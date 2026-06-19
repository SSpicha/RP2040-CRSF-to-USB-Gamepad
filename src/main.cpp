#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <pico/mutex.h>
#include <pico/bootrom.h>
#include <EEPROM.h>
#include <math.h>

#include "transport/CRSF_UART.h"
#include "parser/CRSF_Parser.h"
#include "processing/RC_Processor.h"

// ==================== PINS & CONSTANTS ====================
#define CRSF_RX_PIN     1
#define CRSF_TX_PIN     0
#define CRSF_BAUD       420000
#define LED_PIN         16
#define FAILSAFE_MS     500
#define EEPROM_MAGIC    0x43525346 // 'CRSF' magic constant
#define CONFIG_VERSION  3
#define PROTO_VERSION   "1.0"

// ==================== GLOBALS ====================
enum DeviceMode { MODE_GAMEPAD, MODE_PASSTHROUGH };
volatile DeviceMode currentMode = MODE_GAMEPAD;

CRSFTransport transport;
CRSFParser parser;
RCProcessor processor;
RCProcessor::Config rcConfig;

struct PersistedConfig {
    uint32_t magic;
    uint8_t version;
    RCProcessor::Config cfg;
    uint8_t axisMap[6];
    uint8_t buttonMap[16];
    uint16_t buttonThreshold[16];
} storage;

struct SharedData {
    uint16_t channels[16];
    uint32_t lastPacketTime;
    uint32_t packetCount;
    uint32_t hz;
    bool link;
    uint32_t core1LoopCount;
    uint8_t uplinkRssiDbm;
    uint8_t uplinkLqPct;
    int8_t uplinkSnrDb;
    uint32_t lastLinkStatsTime;
} sharedData;

struct RuntimeStats {
    uint32_t uptimeMs;
    uint32_t lastPacketAgeMs;
    uint32_t loop0Hz;
    uint32_t loop1Hz;
    uint32_t processAvgUs;
    uint32_t processP95Us;
    uint32_t processMaxUs;
    uint32_t linkQualityPct;
    uint32_t transportOverflow;
    uint32_t transportMaxBuffered;
    uint32_t memFree;
    uint32_t mutexMissedReads;
    bool rfStatsValid;
    uint32_t linkStatsAgeMs;
    uint8_t rfRssiDbm;
    uint8_t rfLqPct;
    int8_t rfSnrDb;
} runtimeStats;

uint8_t axisMap[6] = {0, 1, 2, 3, 4, 5}; // x y z rx ry rz
uint8_t buttonMap[16] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 6, 7, 8, 9, 10, 11};
uint16_t buttonThreshold[16] = {
    1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500,
    1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500
};

bool appTelemetrySubscribed = false;
uint32_t appTelemetryIntervalMs = 200;
uint32_t lastAppTelemetryMs = 0;

mutex_t dataMutex;
Adafruit_USBD_HID usb_hid;
Adafruit_NeoPixel pixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);

uint32_t processSamples[64] = {0};
uint8_t processSampleIdx = 0;
uint8_t processSampleCount = 0;

static inline uint8_t clampChannelIndex(int v) {
    if (v < 0) return 0;
    if (v > 15) return 15;
    return (uint8_t)v;
}

void applyDefaultMapping() {
    const uint8_t defaultsAxis[6] = {0, 1, 2, 3, 4, 5};
    for (int i = 0; i < 6; i++) axisMap[i] = defaultsAxis[i];
    for (int i = 0; i < 16; i++) {
        buttonMap[i] = 6 + (i % 10);
        if (buttonMap[i] > 15) buttonMap[i] = 15;
        buttonThreshold[i] = 1500;
    }
}

void validateMapping() {
    for (int i = 0; i < 6; i++) axisMap[i] = clampChannelIndex(axisMap[i]);
    for (int i = 0; i < 16; i++) {
        buttonMap[i] = clampChannelIndex(buttonMap[i]);
        if (buttonThreshold[i] < 900) buttonThreshold[i] = 900;
        if (buttonThreshold[i] > 1900) buttonThreshold[i] = 1900;
    }
}

uint32_t calculateLinkQuality(uint32_t packetRateHz, uint32_t ageMs, bool active) {
    if (!active) return 0;
    float rateScore = packetRateHz / 2.5f; // 250 Hz -> 100%
    if (rateScore > 100.0f) rateScore = 100.0f;
    float agePenalty = ageMs * 0.12f;
    float quality = rateScore - agePenalty;
    if (quality < 0.0f) quality = 0.0f;
    if (quality > 100.0f) quality = 100.0f;
    return (uint32_t)quality;
}

uint32_t calculateLinkQualityFromLq(uint8_t uplinkLqPct, uint32_t linkStatsAgeMs, bool active) {
    if (!active) return 0;
    if (linkStatsAgeMs > 2000) return 0;
    float agePenalty = linkStatsAgeMs * 0.02f;
    float quality = (float)uplinkLqPct - agePenalty;
    if (quality < 0.0f) quality = 0.0f;
    if (quality > 100.0f) quality = 100.0f;
    return (uint32_t)quality;
}

void printJsonStatus(bool includeChannels) {
    uint16_t ch[16];
    uint32_t hz;
    uint32_t lastPacketTime;
    bool link;
    uint8_t rfRssi;
    uint8_t rfLq;
    int8_t rfSnr;
    uint32_t lastLinkStatsTime;

    mutex_enter_blocking(&dataMutex);
    memcpy(ch, sharedData.channels, sizeof(ch));
    hz = sharedData.hz;
    lastPacketTime = sharedData.lastPacketTime;
    link = sharedData.link;
    rfRssi = sharedData.uplinkRssiDbm;
    rfLq = sharedData.uplinkLqPct;
    rfSnr = sharedData.uplinkSnrDb;
    lastLinkStatsTime = sharedData.lastLinkStatsTime;
    mutex_exit(&dataMutex);

    uint32_t now = millis();
    bool active = (link && (now - lastPacketTime < FAILSAFE_MS));
    uint32_t ageMs = active ? (now - lastPacketTime) : FAILSAFE_MS + 1;
    uint32_t linkStatsAgeMs = lastLinkStatsTime > 0 ? (now - lastLinkStatsTime) : 0xFFFFFFFFUL;
    bool rfStatsValid = (lastLinkStatsTime > 0) && (linkStatsAgeMs < 2000);
    CRSFTransport::Stats ts = transport.getStats();
    uint32_t memFree = rp2040.getFreeHeap();

    Serial.printf(
        "{\"type\":\"status\",\"proto\":\"%s\",\"uptime_ms\":%lu,\"mode\":\"%s\","
        "\"link_active\":%s,\"packet_rate_hz\":%lu,\"last_packet_age_ms\":%lu,"
        "\"link_quality_pct\":%lu,\"rf_stats_valid\":%s,\"rf_lq_pct\":%u,"
        "\"rf_uplink_rssi_dbm\":-%u,\"rf_uplink_snr_db\":%d,\"rf_stats_age_ms\":%lu,"
        "\"smoothing\":%s,\"cutoff_hz\":%.1f,"
        "\"deadband\":%u,\"loop0_hz\":%lu,\"loop1_hz\":%lu,\"process_avg_us\":%lu,"
        "\"process_p95_us\":%lu,\"process_max_us\":%lu,\"transport_overflow\":%lu,"
        "\"transport_max_buffered\":%lu,\"mem_free\":%lu",
        PROTO_VERSION,
        now,
        currentMode == MODE_PASSTHROUGH ? "passthrough" : "gamepad",
        active ? "true" : "false",
        hz,
        ageMs,
        runtimeStats.linkQualityPct,
        rfStatsValid ? "true" : "false",
        rfLq,
        rfRssi,
        rfSnr,
        rfStatsValid ? linkStatsAgeMs : 0,
        rcConfig.smoothingEnabled ? "true" : "false",
        rcConfig.smoothingCutoff,
        rcConfig.deadband,
        runtimeStats.loop0Hz,
        runtimeStats.loop1Hz,
        runtimeStats.processAvgUs,
        runtimeStats.processP95Us,
        runtimeStats.processMaxUs,
        ts.overflowCount,
        ts.maxBufferedBytes,
        memFree
    );

    if (includeChannels) {
        Serial.print(",\"channels\":[");
        for (int i = 0; i < 16; i++) {
            Serial.print(ch[i]);
            if (i < 15) Serial.print(",");
        }
        Serial.print("]");
    }
    Serial.println("}");
}

void printJsonMap() {
    Serial.print("{\"type\":\"map\",\"axes\":[");
    for (int i = 0; i < 6; i++) {
        Serial.printf("{\"ch\":%u,\"min\":%u,\"max\":%u,\"invert\":%s}", 
            axisMap[i], 
            rcConfig.axes[i].min, 
            rcConfig.axes[i].max, 
            rcConfig.axes[i].invert ? "true" : "false");
        if (i < 5) Serial.print(",");
    }
    Serial.print("],\"buttons\":[");
    for (int i = 0; i < 16; i++) {
        Serial.printf("{\"idx\":%d,\"ch\":%u,\"th\":%u}", i, buttonMap[i], buttonThreshold[i]);
        if (i < 15) Serial.print(",");
    }
    Serial.println("]}");
}

// ==================== EEPROM HELPERS ====================
void saveConfig() {
    storage.magic = EEPROM_MAGIC;
    storage.version = CONFIG_VERSION;
    storage.cfg = rcConfig;
    memcpy(storage.axisMap, axisMap, sizeof(axisMap));
    memcpy(storage.buttonMap, buttonMap, sizeof(buttonMap));
    memcpy(storage.buttonThreshold, buttonThreshold, sizeof(buttonThreshold));
    EEPROM.put(0, storage);
    EEPROM.commit();
}

void loadConfig() {
    EEPROM.begin(512);
    EEPROM.get(0, storage);
    
    Serial.printf("DEBUG: EEPROM Magic: 0x%08X, Version: %d\n", storage.magic, storage.version);

    if (storage.magic == EEPROM_MAGIC && storage.version >= CONFIG_VERSION) {
        Serial.println("DEBUG: Config loaded successfully.");
        rcConfig = storage.cfg;
        memcpy(axisMap, storage.axisMap, sizeof(axisMap));
        memcpy(buttonMap, storage.buttonMap, sizeof(buttonMap));
        memcpy(buttonThreshold, storage.buttonThreshold, sizeof(buttonThreshold));
    } else {
        Serial.println("DEBUG: Config invalid or outdated. Loading Factory Defaults.");
        // Factory Defaults
        rcConfig.smoothingEnabled = false;
        rcConfig.smoothingCutoff = 50.0f;
        rcConfig.deadband = 4;
        for (int i = 0; i < 6; i++) {
            rcConfig.axes[i].min = 172;
            rcConfig.axes[i].max = 1811;
            rcConfig.axes[i].invert = 0;
        }
        applyDefaultMapping();
        saveConfig(); // Save defaults immediately
    }
    validateMapping();
    processor.setConfig(rcConfig);
}

// ==================== HID DESCRIPTOR ====================
uint8_t const desc_hid_report[] = {
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP ),
    HID_USAGE ( HID_USAGE_DESKTOP_GAMEPAD ),
    HID_COLLECTION ( HID_COLLECTION_APPLICATION ),
        HID_REPORT_ID(1)
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP ),
        HID_USAGE ( HID_USAGE_DESKTOP_X ),
        HID_USAGE ( HID_USAGE_DESKTOP_Y ),
        HID_USAGE ( HID_USAGE_DESKTOP_Z ),
        HID_USAGE ( HID_USAGE_DESKTOP_RX ),
        HID_USAGE ( HID_USAGE_DESKTOP_RY ),
        HID_USAGE ( HID_USAGE_DESKTOP_RZ ),
        HID_LOGICAL_MIN_N ( -32767, 2 ),
        HID_LOGICAL_MAX_N ( 32767, 2 ),
        HID_REPORT_COUNT ( 6 ),
        HID_REPORT_SIZE ( 16 ),
        HID_INPUT ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
        HID_USAGE_PAGE ( HID_USAGE_PAGE_BUTTON ),
        HID_USAGE_MIN ( 1 ),
        HID_USAGE_MAX ( 32 ),
        HID_LOGICAL_MIN ( 0 ),
        HID_LOGICAL_MAX ( 1 ),
        HID_REPORT_COUNT ( 32 ),
        HID_REPORT_SIZE ( 1 ),
        HID_INPUT ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
    HID_COLLECTION_END
};

typedef struct __attribute__((packed)) {
    int16_t x, y, z, rx, ry, rz;
    uint32_t buttons;
} GamepadReport;

// ==================== CORE 1: RECEIVER & PARSER ====================
void setup1() {
    delay(500);
}

void loop1() {
    static CRSFParser::Frame frame;
    static uint32_t lastLinkStatsSeq = 0;
    
    if (currentMode == MODE_GAMEPAD) {
        bool hadData = false;
        while (transport.available()) {
            uint8_t b = transport.read();
            hadData = true;
            if (parser.processByte(b, frame)) {
                mutex_enter_blocking(&dataMutex);
                memcpy(sharedData.channels, frame.channels, sizeof(frame.channels));
                sharedData.lastPacketTime = millis();
                sharedData.packetCount++;
                sharedData.link = true;
                mutex_exit(&dataMutex);
            }
            uint32_t currentSeq = parser.getLinkStatsSequence();
            if (currentSeq != lastLinkStatsSeq) {
                CRSFParser::LinkStats ls;
                if (parser.getLatestLinkStats(ls)) {
                    mutex_enter_blocking(&dataMutex);
                    sharedData.uplinkRssiDbm = ls.uplinkRssi1;
                    sharedData.uplinkLqPct = ls.uplinkLq;
                    sharedData.uplinkSnrDb = ls.uplinkSnr;
                    sharedData.lastLinkStatsTime = millis();
                    mutex_exit(&dataMutex);
                }
                lastLinkStatsSeq = currentSeq;
            }
        }
        if (hadData) {
            mutex_enter_blocking(&dataMutex);
            sharedData.core1LoopCount++;
            mutex_exit(&dataMutex);
        }
    } else {
        delay(10); // Low power mode in passthrough
    }
}

int readIntFromJson(const String& json, const String& key, int start, int& value, int& end) {
    String keyQuoted = "\"" + key + "\"";
    int keyPos = json.indexOf(keyQuoted, start);
    if (keyPos < 0) return -1;
    int colon = json.indexOf(':', keyPos + keyQuoted.length());
    if (colon < 0) return -1;
    int numStart = colon + 1;
    while (numStart < (int)json.length() && (json[numStart] == ' ' || json[numStart] == '\t')) numStart++;
    int numEnd = numStart;
    bool neg = false;
    if (numEnd < (int)json.length() && json[numEnd] == '-') {
        neg = true;
        numEnd++;
    }
    while (numEnd < (int)json.length() && json[numEnd] >= '0' && json[numEnd] <= '9') numEnd++;
    if (numEnd == numStart) return -1;
    value = json.substring(numStart, numEnd).toInt();
    if (neg) value = -value;
    end = numEnd;
    return 0;
}

bool parseMapJson(const String& json, int axes[6], int buttons[16], int thresholds[16], int mins[6], int maxs[6], int invs[6]) {
    int pos = 0;
    for (int i = 0; i < 6; i++) {
        int v = 0;
        int next = 0;
        if (readIntFromJson(json, String("a") + String(i), pos, v, next) != 0) return false;
        if (v < 0 || v >= 16) return false;
        axes[i] = v;
        pos = next;
    }
    for (int i = 0; i < 16; i++) {
        int b = 0;
        int next = 0;
        if (readIntFromJson(json, String("b") + String(i), pos, b, next) != 0) return false;
        if (b < 0 || b >= 16) return false;
        buttons[i] = b;
        pos = next;
    }
    for (int i = 0; i < 16; i++) {
        int t = 1500;
        int next = 0;
        if (readIntFromJson(json, String("t") + String(i), pos, t, next) != 0) {
            thresholds[i] = 1500;
            pos = next > 0 ? next : pos;
            continue;
        }
        if (t < 900 || t > 1900) return false;
        thresholds[i] = t;
        pos = next;
    }
    for (int i = 0; i < 6; i++) {
        int m = 172;
        int next = 0;
        if (readIntFromJson(json, String("min") + String(i), pos, m, next) != 0) {
            mins[i] = 172;
            pos = next > 0 ? next : pos;
            continue;
        }
        if (m < 0 || m > 3000) return false;
        mins[i] = m;
        pos = next;
    }
    for (int i = 0; i < 6; i++) {
        int x = 1811;
        int next = 0;
        if (readIntFromJson(json, String("max") + String(i), pos, x, next) != 0) {
            maxs[i] = 1811;
            pos = next > 0 ? next : pos;
            continue;
        }
        if (x < 0 || x > 3000) return false;
        maxs[i] = x;
        pos = next;
    }
    for (int i = 0; i < 6; i++) {
        int v = 0;
        int next = 0;
        if (readIntFromJson(json, String("inv") + String(i), pos, v, next) != 0) {
            invs[i] = 0;
            pos = next > 0 ? next : pos;
            continue;
        }
        invs[i] = v ? 1 : 0;
        pos = next;
    }
    return true;
}

// ==================== CORE 0: USB & LOGIC ====================
void handleCLI() {
    static String inputBuff = "";
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            inputBuff.trim();
            if (inputBuff.length() > 0) {
                bool changed = false;
                
                if (inputBuff == "version") {
                    Serial.println("Betaflight / RP2040-CRSF-Bridge 4.0.0");
                } else if (inputBuff == "status") {
                    Serial.printf("Link: %s, Rate: %lu Hz, Smoothing: %s, Cutoff: %.1f Hz\n", 
                        sharedData.link ? "YES" : "NO",
                        sharedData.hz,
                        rcConfig.smoothingEnabled ? "ON" : "OFF",
                        rcConfig.smoothingCutoff);
                } else if (inputBuff == "set smoothing on") {
                    rcConfig.smoothingEnabled = true;
                    changed = true;
                    Serial.println("Smoothing: ON");
                } else if (inputBuff == "set smoothing off") {
                    rcConfig.smoothingEnabled = false;
                    changed = true;
                    Serial.println("Smoothing: OFF");
                } else if (inputBuff.startsWith("set cutoff ")) {
                    float cutoff = inputBuff.substring(11).toFloat();
                    if (cutoff > 0) {
                        rcConfig.smoothingCutoff = cutoff;
                        changed = true;
                        Serial.printf("Cutoff: %.1f Hz\n", cutoff);
                    }
                } else if (inputBuff.startsWith("serialpassthrough")) {
                    uint32_t baud = 420000;
                    int lastSpace = inputBuff.lastIndexOf(' ');
                    if (lastSpace > 17) baud = inputBuff.substring(lastSpace).toInt();
                    
                    Serial1.end();
                    Serial1.setRX(CRSF_RX_PIN);
                    Serial1.setTX(CRSF_TX_PIN);
                    Serial1.begin(baud);
                    
                    currentMode = MODE_PASSTHROUGH;
                    Serial.println("Entering Passthrough mode...");
                } else if (inputBuff == "reboot" || inputBuff == "exit") {
                    Serial.println("Rebooting...");
                    delay(200);
                    rp2040.reboot();
                } else if (inputBuff == "dfu" || inputBuff == "bootloader") {
                    Serial.println("Rebooting to Bootloader (USB Mass Storage)...");
                    delay(500);
                    reset_usb_boot(0, 0);
                } else if (inputBuff.startsWith("get ")) {
                    if (inputBuff.indexOf("serialrx_provider") >= 0) Serial.println("serialrx_provider = CRSF");
                    else Serial.println("OK");
                } else if (inputBuff == "app ping") {
                    Serial.println("{\"type\":\"pong\"}");
                } else if (inputBuff == "app get proto") {
                    Serial.printf("{\"type\":\"proto\",\"version\":\"%s\"}\n", PROTO_VERSION);
                } else if (inputBuff == "app get status") {
                    printJsonStatus(false);
                } else if (inputBuff == "app get telemetry") {
                    printJsonStatus(false);
                } else if (inputBuff == "app get channels") {
                    printJsonStatus(true);
                } else if (inputBuff == "app get map") {
                    printJsonMap();
                } else if (inputBuff.startsWith("app sub telemetry ")) {
                    int interval = inputBuff.substring(18).toInt();
                    if (interval < 50) interval = 50;
                    if (interval > 2000) interval = 2000;
                    appTelemetryIntervalMs = (uint32_t)interval;
                    appTelemetrySubscribed = true;
                    Serial.printf("{\"type\":\"ack\",\"stream\":\"telemetry\",\"interval_ms\":%lu}\n", appTelemetryIntervalMs);
                } else if (inputBuff == "app unsub") {
                    appTelemetrySubscribed = false;
                    Serial.println("{\"type\":\"ack\",\"stream\":\"off\"}");
                } else if (inputBuff.startsWith("app set axis ")) {
                    int first = inputBuff.indexOf(' ', 13);
                    if (first > 0) {
                        int axisIdx = inputBuff.substring(13, first).toInt();
                        int ch = inputBuff.substring(first + 1).toInt();
                        if (axisIdx >= 0 && axisIdx < 6 && ch >= 0 && ch < 16) {
                            axisMap[axisIdx] = (uint8_t)ch;
                            changed = true;
                            Serial.println("{\"type\":\"ack\",\"set\":\"axis\"}");
                        } else {
                            Serial.println("{\"type\":\"error\",\"msg\":\"invalid_axis_or_channel\"}");
                        }
                    } else {
                        Serial.println("{\"type\":\"error\",\"msg\":\"bad_format\"}");
                    }
                } else if (inputBuff.startsWith("app set button ")) {
                    int s1 = inputBuff.indexOf(' ', 15);
                    int s2 = s1 > 0 ? inputBuff.indexOf(' ', s1 + 1) : -1;
                    if (s1 > 0 && s2 > 0) {
                        int idx = inputBuff.substring(15, s1).toInt();
                        int ch = inputBuff.substring(s1 + 1, s2).toInt();
                        int th = inputBuff.substring(s2 + 1).toInt();
                        if (idx >= 0 && idx < 16 && ch >= 0 && ch < 16 && th >= 900 && th <= 1900) {
                            buttonMap[idx] = (uint8_t)ch;
                            buttonThreshold[idx] = (uint16_t)th;
                            changed = true;
                            Serial.println("{\"type\":\"ack\",\"set\":\"button\"}");
                        } else {
                            Serial.println("{\"type\":\"error\",\"msg\":\"invalid_button_mapping\"}");
                        }
                    } else {
                        Serial.println("{\"type\":\"error\",\"msg\":\"bad_format\"}");
                    }
                } else if (inputBuff == "app set defaults") {
                    applyDefaultMapping();
                    changed = true;
                    Serial.println("{\"type\":\"ack\",\"set\":\"defaults\"}");
                } else if (inputBuff.startsWith("app set map ")) {
                    String json = inputBuff.substring(12);
                    int axes[6];
                    int buttons[16];
                    int thresholds[16];
                    int mins[6];
                    int maxs[6];
                    int invs[6];
                    bool okAxes = parseMapJson(json, axes, buttons, thresholds, mins, maxs, invs);
                    if (okAxes) {
                        for (int i = 0; i < 6; i++) {
                            axisMap[i] = (uint8_t)axes[i];
                            rcConfig.axes[i].min = (uint16_t)mins[i];
                            rcConfig.axes[i].max = (uint16_t)maxs[i];
                            rcConfig.axes[i].invert = (uint8_t)invs[i];
                        }
                        for (int i = 0; i < 16; i++) {
                            buttonMap[i] = (uint8_t)buttons[i];
                            buttonThreshold[i] = (uint16_t)thresholds[i];
                        }
                        changed = true;
                        Serial.println("{\"type\":\"ack\",\"set\":\"map\"}");
                    } else {
                        Serial.println("{\"type\":\"error\",\"msg\":\"bad_map_json\"}");
                    }
                }


                if (changed) {
                    validateMapping();
                    processor.setConfig(rcConfig);
                    saveConfig();
                }
            }
            inputBuff = "";
            Serial.print("# ");
        } else {
            if (inputBuff.length() < 256) inputBuff += c;
        }
    }
}

void setup() {
    mutex_init(&dataMutex);
    pixel.begin();
    pixel.setBrightness(40);
    runtimeStats.mutexMissedReads = 0;

    loadConfig();

    usb_hid.setPollInterval(1);
    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.begin();

    transport.begin(CRSF_RX_PIN, CRSF_BAUD);
    
    Serial.begin(115200);
}

void loop() {
    uint32_t now = millis();
    static uint32_t loop0Counter = 0;
    loop0Counter++;
    runtimeStats.uptimeMs = now;

    // Hz calculation every 1 second
    static uint32_t lastHzCalc = 0;
    if (now - lastHzCalc >= 1000) {
        uint32_t core1Count = 0;
        mutex_enter_blocking(&dataMutex);
        sharedData.hz = sharedData.packetCount;
        sharedData.packetCount = 0;
        core1Count = sharedData.core1LoopCount;
        sharedData.core1LoopCount = 0;
        mutex_exit(&dataMutex);
        runtimeStats.loop0Hz = loop0Counter;
        runtimeStats.loop1Hz = core1Count;
        loop0Counter = 0;
        lastHzCalc = now;
    }

    if (currentMode == MODE_PASSTHROUGH) {
        static uint32_t lastData = 0;
        pixel.setPixelColor(0, (now / 150 % 2) ? pixel.Color(255, 100, 0) : 0);
        pixel.show();

        while (Serial.available()) {
            Serial1.write(Serial.read());
            lastData = now;
        }
        while (Serial1.available()) {
            Serial.write(Serial1.read());
            lastData = now;
        }

        if (now - lastData > 5000 && lastData != 0) {
            rp2040.reboot();
        }
        return;
    }

    handleCLI();
    
    uint16_t ch[16];
    uint32_t lastTime;
    uint32_t packetRateHz;
    bool link;
    uint8_t rfRssi;
    uint8_t rfLq;
    int8_t rfSnr;
    uint32_t lastLinkStatsTime;

    uint32_t readStart = millis();
    bool gotMutex = false;
    while (!gotMutex) {
        if (mutex_try_enter(&dataMutex, NULL)) {
            gotMutex = true;
            break;
        }
        if (millis() - readStart > 10) {
            runtimeStats.mutexMissedReads++;
            link = false;
            memset(ch, 0, sizeof(ch));
            lastTime = now - FAILSAFE_MS - 1;
            packetRateHz = 0;
            rfRssi = 0;
            rfLq = 0;
            rfSnr = 0;
            lastLinkStatsTime = 0;
            break;
        }
    }
    if (gotMutex) {
        memcpy(ch, sharedData.channels, sizeof(ch));
        lastTime = sharedData.lastPacketTime;
        packetRateHz = sharedData.hz;
        link = sharedData.link;
        rfRssi = sharedData.uplinkRssiDbm;
        rfLq = sharedData.uplinkLqPct;
        rfSnr = sharedData.uplinkSnrDb;
        lastLinkStatsTime = sharedData.lastLinkStatsTime;
        mutex_exit(&dataMutex);
    }

    bool active = (link && (now - lastTime < FAILSAFE_MS));
    runtimeStats.lastPacketAgeMs = active ? now - lastTime : FAILSAFE_MS + 1;
    runtimeStats.linkStatsAgeMs = lastLinkStatsTime > 0 ? (now - lastLinkStatsTime) : 0xFFFFFFFFUL;
    runtimeStats.rfStatsValid = (lastLinkStatsTime > 0) && (runtimeStats.linkStatsAgeMs < 2000);
    runtimeStats.rfRssiDbm = rfRssi;
    runtimeStats.rfLqPct = rfLq;
    runtimeStats.rfSnrDb = rfSnr;
    if (runtimeStats.rfStatsValid) {
        runtimeStats.linkQualityPct = calculateLinkQualityFromLq(rfLq, runtimeStats.linkStatsAgeMs, active);
    } else {
        runtimeStats.linkQualityPct = calculateLinkQuality(packetRateHz, runtimeStats.lastPacketAgeMs, active);
    }
    runtimeStats.transportOverflow = transport.getStats().overflowCount;
    runtimeStats.transportMaxBuffered = transport.getStats().maxBufferedBytes;
    runtimeStats.memFree = rp2040.getFreeHeap();

    if (!active) {
        pixel.setPixelColor(0, (now / 500 % 2) ? pixel.Color(255, 0, 0) : 0);
        pixel.show();
    } else {
        if (rcConfig.smoothingEnabled) pixel.setPixelColor(0, pixel.Color(0, 255, 255));
        else pixel.setPixelColor(0, pixel.Color(0, 255, 0));
        pixel.show();

        static uint32_t lastProcessTime = 0;
        uint32_t curMicros = micros();
        float dt = (curMicros - lastProcessTime) / 1000000.0f;
        if (dt > 0.1f) dt = 0.001f; 
        lastProcessTime = curMicros;

        static GamepadReport report;
        uint32_t t0 = micros();
        report.x  = processor.processAxis(ch[axisMap[0]], 0, dt);
        report.y  = processor.processAxis(ch[axisMap[1]], 1, dt);
        report.z  = processor.processAxis(ch[axisMap[2]], 2, dt);
        report.rx = processor.processAxis(ch[axisMap[3]], 3, dt);
        report.ry = processor.processAxis(ch[axisMap[4]], 4, dt);
        report.rz = processor.processAxis(ch[axisMap[5]], 5, dt);
        
        report.buttons = 0;
        for (int i = 0; i < 16; i++) {
            if (ch[buttonMap[i]] > buttonThreshold[i]) report.buttons |= (1UL << i);
        }

        if (usb_hid.ready()) {
            usb_hid.sendReport(1, &report, sizeof(report));
        }
        uint32_t elapsed = micros() - t0;
        processSamples[processSampleIdx] = elapsed;
        processSampleIdx = (processSampleIdx + 1) % 64;
        if (processSampleCount < 64) processSampleCount++;

        if (processSampleCount > 0) {
            uint32_t sum = 0;
            uint32_t maxv = 0;
            uint32_t sorted[64];
            for (uint8_t i = 0; i < processSampleCount; i++) {
                uint32_t v = processSamples[i];
                sorted[i] = v;
                sum += v;
                if (v > maxv) maxv = v;
            }
            for (uint8_t i = 0; i < processSampleCount; i++) {
                for (uint8_t j = i + 1; j < processSampleCount; j++) {
                    if (sorted[j] < sorted[i]) {
                        uint32_t tmp = sorted[i];
                        sorted[i] = sorted[j];
                        sorted[j] = tmp;
                    }
                }
            }
            uint8_t p95Idx = (uint8_t)((processSampleCount - 1) * 95 / 100);
            runtimeStats.processAvgUs = sum / processSampleCount;
            runtimeStats.processP95Us = sorted[p95Idx];
            runtimeStats.processMaxUs = maxv;
        }
    }

    if (appTelemetrySubscribed && currentMode == MODE_GAMEPAD) {
        if (now - lastAppTelemetryMs >= appTelemetryIntervalMs) {
            printJsonStatus(true);
            lastAppTelemetryMs = now;
        }
    }
}
