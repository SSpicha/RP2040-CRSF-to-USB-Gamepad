#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <pico/bootrom.h>
#include <EEPROM.h>

#include "transport/CRSF_PIO.h"
#include "parser/CRSF_Parser.h"
#include "processing/RC_Processor.h"

#define CRSF_RX_PIN     1
#define CRSF_BAUD       420000
#define LED_PIN         16
#define FAILSAFE_MS     500
#define EEPROM_MAGIC    0x43525346

CRSFTransport transport;
CRSFParser parser;
RCProcessor processor;
RCProcessor::Config rcConfig;

struct SharedData {
    uint16_t channels[16];
    uint32_t lastPacketTime;
    uint32_t packetCount;
    uint32_t hz;
    bool link;
} sharedData;

Adafruit_USBD_HID usb_hid;
Adafruit_NeoPixel pixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);

// Standard Gamepad Descriptor
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_GAMEPAD()
};

void setup() {
    Serial.begin(115200); // USB Serial for diagnostics
    
    pixel.begin();
    pixel.setPixelColor(0, pixel.Color(255, 255, 255)); // White - Start
    pixel.show();

    EEPROM.begin(512);
    EEPROM.get(0, rcConfig);
    if (EEPROM.read(511) != 0xAA) { // Check for factory reset
        rcConfig.smoothingEnabled = true;
        rcConfig.smoothingCutoff = 45.0f;
        rcConfig.deadband = 4;
    }
    processor.setConfig(rcConfig);

    usb_hid.setPollInterval(2); // Relax poll interval for better compatibility
    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.begin();

    transport.begin(CRSF_RX_PIN, CRSF_BAUD);
}

void loop() {
    static uint32_t lastMicros = 0;
    uint32_t nowMicros = micros();
    float dt = (lastMicros == 0) ? 0.001f : (nowMicros - lastMicros) / 1000000.0f;
    lastMicros = nowMicros;

    uint32_t now = millis();

    // 1. Process CRSF Bytes (Single core for now to be safe)
    static CRSFParser::Frame frame;
    while (transport.available()) {
        if (parser.processByte(transport.read(), frame)) {
            memcpy(sharedData.channels, frame.channels, sizeof(frame.channels));
            sharedData.lastPacketTime = now;
            sharedData.packetCount++;
            sharedData.link = true;
        }
    }

    // 2. Failsafe & Stats
    static uint32_t lastStats = 0;
    if (now - lastStats >= 1000) {
        sharedData.hz = sharedData.packetCount;
        sharedData.packetCount = 0;
        lastStats = now;
        if (now - sharedData.lastPacketTime > FAILSAFE_MS) sharedData.link = false;
        
        // Output debug to serial if possible
        if (Serial) Serial.printf("Link: %s, Hz: %lu\n", sharedData.link ? "OK" : "NO", sharedData.hz);
    }

    // 3. LED Status
    if (!TinyUSBDevice.mounted()) {
        pixel.setPixelColor(0, pixel.Color(50, 50, 0)); // Yellow - USB Not mounted
    } else if (!sharedData.link) {
        pixel.setPixelColor(0, (now / 200 % 2) ? pixel.Color(255, 0, 0) : 0); // Red Blinking - No CRSF
    } else {
        pixel.setPixelColor(0, rcConfig.smoothingEnabled ? pixel.Color(0, 100, 255) : pixel.Color(0, 255, 0)); // Cyan/Green - OK
    }
    pixel.show();

    // 4. HID Send
    if (sharedData.link && usb_hid.ready()) {
        hid_gamepad_report_t report = {0};
        
        // Map axes to standard HID (0 to 127/255 or -32767 to 32767 depending on descriptor)
        // TUD_HID_REPORT_DESC_GAMEPAD uses 8-bit (-127 to 127) or 16-bit. 
        // We will scale from our processor.
        
        report.x  = (int8_t)(processor.processAxis(sharedData.channels[0], 0, dt) / 256);
        report.y  = (int8_t)(processor.processAxis(sharedData.channels[1], 1, dt) / 256);
        report.z  = (int8_t)(processor.processAxis(sharedData.channels[4], 4, dt) / 256);
        report.rz = (int8_t)(processor.processAxis(sharedData.channels[5], 5, dt) / 256);
        report.rx = (int8_t)(processor.processAxis(sharedData.channels[3], 3, dt) / 256);
        report.ry = (int8_t)(processor.processThrottle(sharedData.channels[2]) / 256);

        report.buttons = 0;
        for (int i = 0; i < 16; i++) {
            if (sharedData.channels[6 + i] > 1500) report.buttons |= (1 << i);
        }

        usb_hid.sendReport(0, &report, sizeof(report));
    }
}
