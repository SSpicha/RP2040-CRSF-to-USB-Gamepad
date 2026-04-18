#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <pico/mutex.h>
#include <pico/bootrom.h>
#include <EEPROM.h>
#include <math.h>

#include "transport/CRSF_PIO.h"
#include "parser/CRSF_Parser.h"
#include "processing/RC_Processor.h"

// ==================== PINS & CONSTANTS ====================
#define CRSF_RX_PIN     1
#define CRSF_TX_PIN     0
#define CRSF_BAUD       420000
#define LED_PIN         16
#define FAILSAFE_MS     500
#define EEPROM_MAGIC    0x43525346 // 'CRSF' magic constant
#define CONFIG_VERSION  2
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
    bool rfStatsValid;
    uint32_t linkStatsAgeMs;
    uint8_t rfRssiDbm;
    uint8_t rfLqPct;
    int8_t rfSnrDb;
} runtimeStats;

uint8_t axisMap[6] = {0, 1, 4, 5, 3, 2}; // x y z rz rx ry
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
    const uint8_t defaultsAxis[6] = {0, 1, 4, 5, 3, 2};
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
        Serial.print(axisMap[i]);
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
void loadConfig() {
    EEPROM.begin(512);
    EEPROM.get(0, storage);

    if (storage.magic == EEPROM_MAGIC && storage.version >= CONFIG_VERSION) {
        rcConfig = storage.cfg;
        memcpy(axisMap, storage.axisMap, sizeof(axisMap));
        memcpy(buttonMap, storage.buttonMap, sizeof(buttonMap));
        memcpy(buttonThreshold, storage.buttonThreshold, sizeof(buttonThreshold));
    } else {
        // Factory Defaults
        rcConfig.smoothingEnabled = false;
        rcConfig.smoothingCutoff = 50.0f;
        rcConfig.deadband = 4;
        applyDefaultMapping();
    }
    validateMapping();
    processor.setConfig(rcConfig);
}

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
        HID_USAGE ( HID_USAGE_DESKTOP_RZ ),
        HID_USAGE ( HID_USAGE_DESKTOP_RX ),
        HID_USAGE ( HID_USAGE_DESKTOP_RY ),
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
    int16_t x, y, z, rz, rx, ry;
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
                    if (inputBuff.contains("serialrx_provider")) Serial.println("serialrx_provider = CRSF");
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
            inputBuff += c;
        }
    }
}

void setup() {
    mutex_init(&dataMutex);
    pixel.begin();
    pixel.setBrightness(40);

    loadConfig();

    usb_hid.setPollInterval(1);
    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.begin();

    transport.begin(CRSF_RX_PIN, CRSF_BAUD);
    
    Serial1.setRX(CRSF_RX_PIN);
    Serial1.setTX(CRSF_TX_PIN);
    Serial1.begin(CRSF_BAUD);
    
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

    mutex_enter_blocking(&dataMutex);
    memcpy(ch, sharedData.channels, sizeof(ch));
    lastTime = sharedData.lastPacketTime;
    packetRateHz = sharedData.hz;
    link = sharedData.link;
    rfRssi = sharedData.uplinkRssiDbm;
    rfLq = sharedData.uplinkLqPct;
    rfSnr = sharedData.uplinkSnrDb;
    lastLinkStatsTime = sharedData.lastLinkStatsTime;
    mutex_exit(&dataMutex);

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

        static GamepadReport report;
        uint32_t t0 = micros();
        report.x  = processor.processAxis(ch[axisMap[0]], 0);
        report.y  = processor.processAxis(ch[axisMap[1]], 1);
        report.z  = processor.processAxis(ch[axisMap[2]], 2);
        report.rz = processor.processAxis(ch[axisMap[3]], 3);
        report.rx = processor.processAxis(ch[axisMap[4]], 4);
        report.ry = processor.processThrottle(ch[axisMap[5]]);
        
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
