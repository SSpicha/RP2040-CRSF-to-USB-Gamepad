#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <pico/mutex.h>
#include <pico/bootrom.h>
#include <EEPROM.h>

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

// ==================== GLOBALS ====================
enum DeviceMode { MODE_GAMEPAD, MODE_PASSTHROUGH };
volatile DeviceMode currentMode = MODE_GAMEPAD;

CRSFTransport transport;
CRSFParser parser;
RCProcessor processor;
RCProcessor::Config rcConfig;

struct PersistedConfig {
    uint32_t magic;
    RCProcessor::Config cfg;
} storage;

struct SharedData {
    uint16_t channels[16];
    uint32_t lastPacketTime;
    uint32_t packetCount;
    uint32_t hz;
    bool link;
} sharedData;

mutex_t dataMutex;
Adafruit_USBD_HID usb_hid;
Adafruit_NeoPixel pixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);

// ==================== EEPROM HELPERS ====================
void loadConfig() {
    EEPROM.begin(512);
    EEPROM.get(0, storage);
    
    if (storage.magic == EEPROM_MAGIC) {
        rcConfig = storage.cfg;
    } else {
        // Factory Defaults
        rcConfig.smoothingEnabled = false;
        rcConfig.smoothingCutoff = 50.0f;
        rcConfig.deadband = 4;
    }
    processor.setConfig(rcConfig);
}

void saveConfig() {
    storage.magic = EEPROM_MAGIC;
    storage.cfg = rcConfig;
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
    
    if (currentMode == MODE_GAMEPAD) {
        while (transport.available()) {
            uint8_t b = transport.read();
            if (parser.processByte(b, frame)) {
                mutex_enter_blocking(&dataMutex);
                memcpy(sharedData.channels, frame.channels, sizeof(frame.channels));
                sharedData.lastPacketTime = millis();
                sharedData.packetCount++;
                sharedData.link = true;
                mutex_exit(&dataMutex);
            }
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
                    Serial.println("RP2040-CRSF-Bridge 4.0.1-fixed");
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
                }

                if (changed) {
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
    
    // Serial1 is NOT started here to avoid GPIO conflict with PIO
    Serial.begin(115200);
}

void loop() {
    static uint32_t lastLoopTime = 0;
    uint32_t now = millis();
    float dt = (now - lastLoopTime) / 1000.0f;
    lastLoopTime = now;

    // Hz calculation every 1 second
    static uint32_t lastHzCalc = 0;
    if (now - lastHzCalc >= 1000) {
        mutex_enter_blocking(&dataMutex);
        sharedData.hz = sharedData.packetCount;
        sharedData.packetCount = 0;
        mutex_exit(&dataMutex);
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
    bool link;

    mutex_enter_blocking(&dataMutex);
    memcpy(ch, sharedData.channels, sizeof(ch));
    lastTime = sharedData.lastPacketTime;
    
    // Failsafe & link status update
    if (now - lastTime > FAILSAFE_MS) {
        sharedData.link = false;
    }
    link = sharedData.link;
    mutex_exit(&dataMutex);

    if (!link) {
        pixel.setPixelColor(0, (now / 500 % 2) ? pixel.Color(255, 0, 0) : 0);
        pixel.show();
    } else {
        if (rcConfig.smoothingEnabled) pixel.setPixelColor(0, pixel.Color(0, 255, 255));
        else pixel.setPixelColor(0, pixel.Color(0, 255, 0));
        pixel.show();

        static GamepadReport report;
        report.x  = processor.processAxis(ch[0], 0, dt);
        report.y  = processor.processAxis(ch[1], 1, dt);
        report.ry = processor.processThrottle(ch[2]);
        report.rx = processor.processAxis(ch[3], 3, dt);
        report.z  = processor.processAxis(ch[4], 4, dt);
        report.rz = processor.processAxis(ch[5], 5, dt);
        
        report.buttons = 0;
        for (int i = 0; i < 10; i++) {
            if (ch[6 + i] > 1500) report.buttons |= (1UL << i);
        }

        if (usb_hid.ready()) {
            usb_hid.sendReport(1, &report, sizeof(report));
        }
    }
}
