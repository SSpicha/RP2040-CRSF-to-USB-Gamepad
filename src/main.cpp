#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <pico/mutex.h>

// ==================== PINS & CONSTANTS ====================
#define CRSF_RX_PIN     1
#define CRSF_TX_PIN     0
#define CRSF_BAUD       420000
#define LED_PIN         16 // WS2812 Neopixel pin

#define FAILSAFE_TIMEOUT_MS   500
#define DEADBAND_THRESHOLD    4

// ==================== SHARED DATA (CORE 0 <-> CORE 1) ====================
typedef struct {
  uint16_t channels[16];
  uint32_t lastPacketTime;
  bool linkEstablished;
} shared_data_t;

shared_data_t sharedData;
mutex_t dataMutex;
mutex_t uartMutex;

// ==================== MODES & CLI ====================
enum DeviceMode { MODE_GAMEPAD, MODE_PASSTHROUGH };
volatile DeviceMode currentMode = MODE_GAMEPAD;

static char serialInBuff[64];
static uint8_t serialInBuffLen = 0;
static bool serialEcho = false;

// ==================== USB & HID OBJECTS ====================
uint8_t const desc_hid_report[] = {
  HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP     )                 ,
  HID_USAGE      ( HID_USAGE_DESKTOP_GAMEPAD  )                 ,
  HID_COLLECTION ( HID_COLLECTION_APPLICATION )                 ,
    HID_REPORT_ID(1)
    HID_USAGE_PAGE     ( HID_USAGE_PAGE_DESKTOP                 ) ,
    HID_USAGE          ( HID_USAGE_DESKTOP_X                    ) ,
    HID_USAGE          ( HID_USAGE_DESKTOP_Y                    ) ,
    HID_USAGE          ( HID_USAGE_DESKTOP_Z                    ) ,
    HID_USAGE          ( HID_USAGE_DESKTOP_RZ                   ) ,
    HID_USAGE          ( HID_USAGE_DESKTOP_RX                   ) ,
    HID_USAGE          ( HID_USAGE_DESKTOP_RY                   ) ,
    HID_LOGICAL_MIN_N  ( -32767, 2                              ) ,
    HID_LOGICAL_MAX_N  ( 32767, 2                               ) ,
    HID_REPORT_COUNT   ( 6                                      ) ,
    HID_REPORT_SIZE    ( 16                                     ) ,
    HID_INPUT          ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,
    HID_USAGE_PAGE     ( HID_USAGE_PAGE_DESKTOP                 ) ,
    HID_USAGE          ( HID_USAGE_DESKTOP_HAT_SWITCH           ) ,
    HID_LOGICAL_MIN    ( 0                                      ) ,
    HID_LOGICAL_MAX    ( 7                                      ) ,
    HID_PHYSICAL_MIN   ( 0                                      ) ,
    HID_PHYSICAL_MAX_N ( 315, 2                                 ) ,
    HID_REPORT_COUNT   ( 1                                      ) ,
    HID_REPORT_SIZE    ( 8                                      ) ,
    HID_INPUT          ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,
    HID_USAGE_PAGE     ( HID_USAGE_PAGE_BUTTON                  ) ,
    HID_USAGE_MIN      ( 1                                      ) ,
    HID_USAGE_MAX      ( 32                                     ) ,
    HID_LOGICAL_MIN    ( 0                                      ) ,
    HID_LOGICAL_MAX    ( 1                                      ) ,
    HID_REPORT_COUNT   ( 32                                     ) ,
    HID_REPORT_SIZE    ( 1                                      ) ,
    HID_INPUT          ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,
  HID_COLLECTION_END
};

Adafruit_USBD_HID usb_hid;
Adafruit_NeoPixel pixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);

typedef struct __attribute__((packed)) {
  int16_t x, y, z, rz, rx, ry;
  uint8_t hat;
  uint32_t buttons;
} gamepad_report_t;

gamepad_report_t gpReport = {0};
gamepad_report_t lastGpReport = {0};

// ==================== CRSF PARSER ====================
static const uint8_t CRSF_ADDR_RADIO = 0xC8;
static const uint8_t CRSF_TYPE_RC_CHANNELS = 0x16;

enum ParseState { WAIT_ADDR, WAIT_LEN, WAIT_PAYLOAD };
ParseState parseState = WAIT_ADDR;
uint8_t frameBuf[64];
uint8_t frameLen = 0;
uint8_t framePos = 0;

static const uint8_t crsf_crc8_table[256] = {
  0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54, 0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
  0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06, 0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
  0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0, 0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
  0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2, 0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
  0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9, 0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
  0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B, 0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
  0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D, 0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
  0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F, 0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
  0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB, 0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
  0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9, 0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
  0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F, 0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
  0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D, 0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
  0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26, 0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
  0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74, 0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
  0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82, 0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
  0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0, 0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9
};

uint8_t crsfCrc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    crc = crsf_crc8_table[crc ^ data[i]];
  }
  return crc;
}

void processCrsfByte(uint8_t b) {
  switch (parseState) {
    case WAIT_ADDR:
      if (b == CRSF_ADDR_RADIO || b == 0xEA || b == 0xEE) {
        frameBuf[0] = b;
        framePos = 1;
        parseState = WAIT_LEN;
      }
      break;
    case WAIT_LEN:
      frameLen = b;
      if (frameLen < 2 || frameLen > 62) {
        parseState = WAIT_ADDR;
      } else {
        parseState = WAIT_PAYLOAD;
      }
      break;
    case WAIT_PAYLOAD:
      frameBuf[framePos++] = b;
      if (framePos == frameLen + 1) {
        uint8_t crcCalc = crsfCrc8(&frameBuf[1], frameLen - 1);
        if (crcCalc == frameBuf[framePos - 1]) {
          uint8_t type = frameBuf[1];
          if (type == CRSF_TYPE_RC_CHANNELS && frameLen == 24) {
            const uint8_t *p = &frameBuf[2];
            uint16_t tempChannels[16];
            for (int ch = 0; ch < 16; ch++) {
              uint16_t bits = ch * 11;
              uint16_t idx = bits >> 3;
              uint8_t  off = bits & 7;
              uint32_t val = p[idx];
              if (idx + 1 < 22) val |= p[idx + 1] << 8;
              if (idx + 2 < 22) val |= p[idx + 2] << 16;
              tempChannels[ch] = (val >> off) & 0x7FF;
            }
            mutex_enter_blocking(&dataMutex);
            memcpy(sharedData.channels, tempChannels, sizeof(tempChannels));
            sharedData.lastPacketTime = millis();
            sharedData.linkEstablished = true;
            mutex_exit(&dataMutex);
          }
        }
        parseState = WAIT_ADDR;
      }
      break;
  }
}

// ==================== CORE 1 ====================
void setup1() { delay(100); }
void loop1() {
  __dmb();
  if (currentMode == MODE_GAMEPAD) {
    if (mutex_enter_timeout_ms(&uartMutex, 2)) {
      int limit = 64; 
      while (Serial1.available() && limit--) { 
        processCrsfByte(Serial1.read()); 
      }
      mutex_exit(&uartMutex);
    }
  } else { delay(1); }
}

// ==================== MAPPING ====================
#define CRSF_CENTER 992
#define CRSF_MIN    172
#define CRSF_MAX    1811

int16_t mapAxis(uint16_t v) {
  if (abs((int)v - CRSF_CENTER) < DEADBAND_THRESHOLD) return 0;
  int32_t val;
  if (v >= CRSF_CENTER) val = ((int32_t)(v - CRSF_CENTER) * 32767) / (CRSF_MAX - CRSF_CENTER);
  else val = ((int32_t)(v - CRSF_CENTER) * 32767) / (CRSF_CENTER - CRSF_MIN);
  if (val > 32767) val = 32767;
  if (val < -32767) val = -32767;
  return (int16_t)val;
}

int16_t mapThrottle(uint16_t v) {
  int32_t val = ((int32_t)(v - CRSF_MIN) * 65534) / (CRSF_MAX - CRSF_MIN) - 32767;
  if (val > 32767) val = 32767;
  if (val < -32767) val = -32767;
  return (int16_t)val;
}

void updateLED(bool link, bool passthrough, uint32_t lastPacket) {
  static uint32_t lastColor = 0;
  uint32_t now = millis();
  uint32_t newColor = 0;

  if (passthrough) {
    newColor = (now / 150 % 2) ? pixel.Color(255, 100, 0) : 0;
  } else if (!link || (now - lastPacket > FAILSAFE_TIMEOUT_MS)) {
    newColor = (now / 500 % 2) ? pixel.Color(255, 0, 0) : 0;
  } else {
    newColor = pixel.Color(0, 255, 0);
  }

  if (newColor != lastColor) {
    pixel.setPixelColor(0, newColor);
    pixel.show();
    lastColor = newColor;
  }
}

// ==================== CLI HANDLER ====================
bool handleSerialCommand(char *cmd) {
    if (strcmp(cmd, "version") == 0) {
        Serial.println("Betaflight / RP2040-CRSF-Bridge 4.0.0");
    } 
    else if (strcmp(cmd, "status") == 0) {
        Serial.println("System Uptime: 1337s, CPU Load: 1%");
    }
    else if (strcmp(cmd, "serial") == 0) {
        Serial.println("serial 0 64 115200 57600 0 115200");
    }
    else if (strcmp(cmd, "get serialrx_provider") == 0) {
        Serial.println("serialrx_provider = CRSF");
    }
    else if (strcmp(cmd, "get serialrx_inverted") == 0) {
        Serial.println("serialrx_inverted = OFF");
    }
    else if (strcmp(cmd, "get serialrx_halfduplex") == 0) {
        Serial.println("serialrx_halfduplex = OFF");
    }
    else if (strncmp(cmd, "get ", 4) == 0) {
        // Catch-all for other 'get' commands to keep the configurator happy
    }
    else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "exit") == 0 || strcmp(cmd, "save") == 0) {
        Serial.println("Rebooting...");
        delay(200);
        rp2040.reboot();
    }
    else if (strncmp(cmd, "serialpassthrough", 17) == 0) {
        uint32_t baud = 420000;
        char *p = cmd + 17;
        while (*p == ' ') p++; // skip spaces
        if (*p) {
            // skip index
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++; // skip spaces
            if (*p) {
                baud = atol(p);
            }
        }
        
        if (baud > 0) {
            Serial1.begin(baud);
        }
        
        currentMode = MODE_PASSTHROUGH;
        __dmb();
        return true; // Mode changed
    }
    return false;
}

void checkSerialIn() {
    while (Serial.available()) {
        char c = Serial.read();
        
        if (c == '#') {
            serialInBuffLen = 0;
            serialEcho = true;
            Serial.print("\r\n# ");
            continue;
        }

        if (serialEcho && c != '\r' && c != '\n' && c != '\b') {
            Serial.write(c);
        }

        if (c == '\r' || c == '\n') {
            Serial.print("\r\n");
            if (serialInBuffLen > 0) {
                serialInBuff[serialInBuffLen] = '\0';
                bool modeChanged = handleSerialCommand(serialInBuff);
                serialInBuffLen = 0;
                if (modeChanged) return;
            }
            Serial.print("# ");
        }
        else if (c == '\b' || c == 127) { // backspace
            if (serialInBuffLen > 0) {
                serialInBuffLen--;
                if (serialEcho) Serial.print("\b \b");
            }
        }
        else {
            if (serialInBuffLen < sizeof(serialInBuff) - 1) {
                serialInBuff[serialInBuffLen++] = c;
            }
        }
    }
}

// ==================== CORE 0 ====================
void setup() {
  mutex_init(&dataMutex);
  mutex_init(&uartMutex);
  pixel.begin();
  pixel.setBrightness(40);
  pixel.show();

  mutex_enter_blocking(&dataMutex);
  for (int i = 0; i < 16; i++) sharedData.channels[i] = CRSF_CENTER;
  sharedData.channels[2] = CRSF_MIN;
  sharedData.lastPacketTime = 0;
  sharedData.linkEstablished = false;
  mutex_exit(&dataMutex);

  usb_hid.setPollInterval(1);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  Serial1.setRX(CRSF_RX_PIN);
  Serial1.setTX(CRSF_TX_PIN);
  Serial1.begin(CRSF_BAUD);
  Serial1.setFIFOSize(256);
  
  while (!TinyUSBDevice.mounted()) delay(1);
}

void loop() {
  uint32_t now = millis();
  uint16_t localChannels[16];
  uint32_t localLastPacketTime;
  bool localLinkEstablished;

  // Process CLI
  if (currentMode == MODE_GAMEPAD) {
      checkSerialIn();
  }

  mutex_enter_blocking(&dataMutex);
  memcpy(localChannels, sharedData.channels, sizeof(localChannels));
  localLastPacketTime = sharedData.lastPacketTime;
  localLinkEstablished = sharedData.linkEstablished;
  mutex_exit(&dataMutex);

  if (localLinkEstablished && (now - localLastPacketTime > FAILSAFE_TIMEOUT_MS)) {
    for (int i = 0; i < 16; i++) localChannels[i] = CRSF_CENTER;
    localChannels[2] = CRSF_MIN;
  }

  if (currentMode == MODE_PASSTHROUGH) {
    uint8_t buffer[128];
    int available;
    static uint32_t lastDataTime = 0;

    if (mutex_enter_timeout_ms(&uartMutex, 1)) {
      if ((available = Serial.available()) > 0) {
        int count = Serial.readBytes(buffer, min(available, (int)sizeof(buffer)));
        Serial1.write(buffer, count);
        lastDataTime = now;
      }
      if ((available = Serial1.available()) > 0) {
        int count = Serial1.readBytes(buffer, min(available, (int)sizeof(buffer)));
        for (int i = 0; i < count; i++) { processCrsfByte(buffer[i]); }
        Serial.write(buffer, count);
        lastDataTime = now;
      }
      mutex_exit(&uartMutex);
    }

    // Auto-exit passthrough if no data for 5 seconds (safety)
    if (now - lastDataTime > 5000 && lastDataTime != 0) {
        currentMode = MODE_GAMEPAD;
        serialEcho = false;
        __dmb();
    }

  } else {
    // Mapping Logic
    gpReport.x  = mapAxis(localChannels[0]);
    gpReport.y  = mapAxis(localChannels[1]);
    gpReport.z  = mapAxis(localChannels[4]);
    gpReport.rz = mapAxis(localChannels[5]);
    gpReport.rx = mapAxis(localChannels[3]);
    gpReport.ry = mapThrottle(localChannels[2]);
    gpReport.hat = 8; 
    gpReport.buttons = 0;

    for (int i = 0; i < 10; i++) {
      if (localChannels[6 + i] > 1500) {
        gpReport.buttons |= (1UL << i);
      }
    }

    if (memcmp(&gpReport, &lastGpReport, sizeof(gamepad_report_t)) != 0) {
      if (usb_hid.ready()) {
        usb_hid.sendReport(1, &gpReport, sizeof(gpReport));
        memcpy(&lastGpReport, &gpReport, sizeof(gamepad_report_t));
      }
    }
  }
  updateLED(localLinkEstablished, currentMode == MODE_PASSTHROUGH, localLastPacketTime);
}