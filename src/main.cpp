#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <pico/mutex.h>
#include <EEPROM.h>

#include "processing/RC_Processor.h"

// ==================== PINS & CONSTANTS ====================
#define CRSF_RX_PIN     1
#define CRSF_TX_PIN     0
#define CRSF_BAUD       420000
#define LED_PIN         16
#define FAILSAFE_MS     500

// ==================== SHARED DATA ====================
struct SharedData {
  uint16_t channels[16];
  uint32_t lastPacketTime;
  bool link;
} sharedData;

mutex_t dataMutex;
DeviceMode currentMode = MODE_GAMEPAD;

RCProcessor processor;
RCProcessor::Config rcConfig;
Adafruit_USBD_HID usb_hid;
Adafruit_NeoPixel pixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);

// ==================== HID DESCRIPTOR (Standard Gamepad) ====================
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
        HID_USAGE_MAX ( 16 ),
        HID_LOGICAL_MIN ( 0 ),
        HID_LOGICAL_MAX ( 1 ),
        HID_REPORT_COUNT ( 16 ),
        HID_REPORT_SIZE ( 1 ),
        HID_INPUT ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
    HID_COLLECTION_END
};

typedef struct __attribute__((packed)) {
  int16_t x, y, z, rz, rx, ry;
  uint16_t buttons;
} GamepadReport;

// ==================== CRSF PARSER (Optimized) ====================
void processCrsfByte(uint8_t b) {
    static uint8_t frameBuf[64];
    static uint8_t framePos = 0;
    static uint8_t frameLen = 0;
    static enum { WAIT_ADDR, WAIT_LEN, WAIT_PAYLOAD } state = WAIT_ADDR;

    switch (state) {
        case WAIT_ADDR:
            if (b == 0xC8 || b == 0xEA || b == 0xEE) {
                frameBuf[0] = b;
                framePos = 1;
                state = WAIT_LEN;
            }
            break;
        case WAIT_LEN:
            frameLen = b;
            if (frameLen < 2 || frameLen > 62) state = WAIT_ADDR;
            else state = WAIT_PAYLOAD;
            break;
        case WAIT_PAYLOAD:
            frameBuf[framePos++] = b;
            if (framePos == frameLen + 1) {
                if (frameBuf[1] == 0x16 && frameLen == 24) {
                    uint16_t temp[16];
                    const uint8_t *p = &frameBuf[2];
                    temp[0] = (p[0] | p[1] << 8) & 0x07FF;
                    temp[1] = (p[1] >> 3 | p[2] << 5) & 0x07FF;
                    temp[2] = (p[2] >> 6 | p[3] << 2 | p[4] << 10) & 0x07FF;
                    temp[3] = (p[4] >> 1 | p[5] << 7) & 0x07FF;
                    temp[4] = (p[5] >> 4 | p[6] << 4) & 0x07FF;
                    temp[5] = (p[6] >> 7 | p[7] << 1 | p[8] << 9) & 0x07FF;
                    temp[6] = (p[8] >> 2 | p[9] << 6) & 0x07FF;
                    temp[7] = (p[9] >> 5 | p[10] << 3) & 0x07FF;
                    temp[8] = (p[11] | p[12] << 8) & 0x07FF;
                    temp[9] = (p[12] >> 3 | p[13] << 5) & 0x07FF;
                    temp[10] = (p[13] >> 6 | p[14] << 2 | p[15] << 10) & 0x07FF;
                    temp[11] = (p[15] >> 1 | p[16] << 7) & 0x07FF;
                    temp[12] = (p[16] >> 4 | p[17] << 4) & 0x07FF;
                    temp[13] = (p[17] >> 7 | p[18] << 1 | p[19] << 9) & 0x07FF;
                    temp[14] = (p[19] >> 2 | p[20] << 6) & 0x07FF;
                    temp[15] = (p[20] >> 5 | p[21] << 3) & 0x07FF;

                    mutex_enter_blocking(&dataMutex);
                    memcpy(sharedData.channels, temp, sizeof(temp));
                    sharedData.lastPacketTime = millis();
                    sharedData.link = true;
                    mutex_exit(&dataMutex);
                }
                state = WAIT_ADDR;
            }
            break;
    }
}

// ==================== CORE 1: RX (AS IN MAIN) ====================
void setup1() { delay(100); }
void loop1() {
    if (currentMode == MODE_GAMEPAD) {
        while (Serial1.available()) {
            processCrsfByte(Serial1.read());
        }
    } else {
        delay(1);
    }
}

// ==================== CORE 0: USB & LOGIC ====================
void setup() {
    mutex_init(&dataMutex);
    pixel.begin();
    pixel.setBrightness(40);

    EEPROM.begin(512);
    EEPROM.get(0, rcConfig);
    if (EEPROM.read(511) != 0xAA) {
        rcConfig.smoothingEnabled = true;
        rcConfig.smoothingCutoff = 45.0f;
        rcConfig.deadband = 4;
        EEPROM.put(0, rcConfig);
        EEPROM.write(511, 0xAA);
        EEPROM.commit();
    }
    processor.setConfig(rcConfig);

    usb_hid.setPollInterval(1);
    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.begin();

    Serial1.setRX(CRSF_RX_PIN);
    Serial1.setTX(CRSF_TX_PIN);
    Serial1.begin(CRSF_BAUD);
    Serial1.setFIFOSize(256);

    Serial.begin(115200);
}

void loop() {
    static uint32_t lastMicros = 0;
    uint32_t nowMicros = micros();
    float dt = (lastMicros == 0) ? 0.001f : (nowMicros - lastMicros) / 1000000.0f;
    lastMicros = nowMicros;

    uint32_t now = millis();

    uint16_t ch[16];
    uint32_t lastPacket;
    bool link;

    mutex_enter_blocking(&dataMutex);
    memcpy(ch, sharedData.channels, sizeof(ch));
    lastPacket = sharedData.lastPacketTime;
    if (now - lastPacket > FAILSAFE_MS) sharedData.link = false;
    link = sharedData.link;
    mutex_exit(&dataMutex);

    // Update LED (as in main)
    if (!link) {
        pixel.setPixelColor(0, (now / 500 % 2) ? pixel.Color(255, 0, 0) : 0);
    } else {
        pixel.setPixelColor(0, rcConfig.smoothingEnabled ? pixel.Color(0, 100, 255) : pixel.Color(0, 255, 0));
    }
    pixel.show();

    // Send HID Report
    if (link && usb_hid.ready()) {
        static GamepadReport report;
        report.x  = processor.processAxis(ch[0], 0, dt);
        report.y  = processor.processAxis(ch[1], 1, dt);
        report.ry = processor.processThrottle(ch[2]);
        report.rx = processor.processAxis(ch[3], 3, dt);
        report.z  = processor.processAxis(ch[4], 4, dt);
        report.rz = processor.processAxis(ch[5], 5, dt);
        
        report.buttons = 0;
        for (int i = 0; i < 16; i++) {
            if (ch[6 + i] > 1500) report.buttons |= (1 << i);
        }
        usb_hid.sendReport(1, &report, sizeof(report));
    }
}
