#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ==================== PINS ====================
#define CRSF_RX_PIN     1
#define CRSF_TX_PIN     0
#define CRSF_BAUD       420000

#define OLED_SDA_PIN    4
#define OLED_SCL_PIN    5
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1

#define NEOPIXEL_PIN    23
#define NUM_PIXELS      1

#define USER_BUTTON_PIN 24
#define BUILTIN_LED     25

// ==================== MODES ==================
enum DeviceMode { MODE_GAMEPAD, MODE_PASSTHROUGH };
DeviceMode currentMode = MODE_GAMEPAD;

#define CH_PASSTHROUGH_TOGGLE 9   // Channel 9 > 1500 → Passthrough

// ==================== SCREEN =================
uint8_t screenPage = 0;
const uint8_t MAX_PAGES = 4;

// ==================== OBJECTS ================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel pixel(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(1)) };
Adafruit_USBD_HID usb_hid;

// Gamepad report
typedef struct __attribute__((packed)) {
  int8_t  x, y, z, rz, rx, ry;
  uint8_t hat;
  uint32_t buttons;
} gamepad_report_t;
gamepad_report_t gpReport = {0};

// ==================== CRSF ===================
static const uint8_t CRSF_ADDR_RADIO = 0xC8;
static const uint8_t CRSF_TYPE_RC_CHANNELS = 0x16;
static const uint8_t CRSF_TYPE_LINK_STATS  = 0x14;

typedef struct {
  uint8_t uplink_rssi_1;
  uint8_t uplink_rssi_2;
  uint8_t uplink_link_quality;
  int8_t  uplink_snr;
  uint8_t active_antenna;
  uint8_t rf_mode;
  uint8_t uplink_tx_power;
  uint8_t downlink_rssi;
  uint8_t downlink_link_quality;
  int8_t  downlink_snr;
} crsf_link_stats_t;

crsf_link_stats_t linkStats = {0};
bool linkEstablished = false;
uint16_t channels[16];

// TX power table (ELRS / Crossfire)
const uint16_t txPowerTable[] = {0, 10, 25, 50, 100, 250, 500, 1000, 2000};
#define TX_POWER_COUNT (sizeof(txPowerTable)/sizeof(txPowerTable[0]))

// ==================== PARSER =================
enum ParseState { WAIT_ADDR, WAIT_LEN, WAIT_PAYLOAD };
ParseState parseState = WAIT_ADDR;
uint8_t frameBuf[64];
uint8_t frameLen = 0;
uint8_t framePos = 0;

// ==================== BUTTON =================
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;

// For channel 10 ( momentary >1500 → switch page)
bool lastChannel10High = false;

void handleButton() {
  int reading = digitalRead(USER_BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading == LOW && lastButtonState == HIGH) {
      screenPage = (screenPage + 1) % MAX_PAGES;
    }
  }
  lastButtonState = reading;
}

// ==================== CRC8 ===================
uint8_t crsfCrc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (crc << 1) ^ 0xD5 : (crc << 1);
    }
  }
  return crc;
}

// ==================== CRSF PARSER ============
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
      if (framePos == frameLen + 1) {  // len includes type+payload+crc, pos starts after addr
        uint8_t crcCalc = crsfCrc8(&frameBuf[1], frameLen - 1);  // type + payload
        if (crcCalc == frameBuf[framePos - 1]) {
          uint8_t type = frameBuf[1];

          if (type == CRSF_TYPE_RC_CHANNELS && frameLen == 24) {
            const uint8_t *p = &frameBuf[2];
            for (int ch = 0; ch < 16; ch++) {
              uint16_t bits = ch * 11;
              uint16_t idx = bits / 8;
              uint8_t  off = bits % 8;
              uint32_t val = p[idx];
              if (idx + 1 < 22) val |= p[idx + 1] << 8;
              if (idx + 2 < 22) val |= p[idx + 2] << 16;
              channels[ch] = (val >> off) & 0x7FF;
            }
            linkEstablished = true;
          }

          if (type == CRSF_TYPE_LINK_STATS && frameLen >= 12) {
            memcpy(&linkStats, &frameBuf[2], sizeof(linkStats));
          }
        }
        parseState = WAIT_ADDR;
      }
      break;
  }
}

// ==================== MAPPING ================
int8_t mapAxis(uint16_t v) {
  int32_t c = v - 1024;
  return (int8_t)constrain((c * 127L) / 1024L, -127, 127);
}

int8_t mapThrottle(uint16_t v) {
  return (int8_t)constrain(((int32_t)v * 254L / 2047L) - 127, -127, 127);
}

// ==================== SCREEN =================
void drawScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(0, 0);
  display.print(currentMode == MODE_GAMEPAD ? "GAMEPAD" : "PASSTHROUGH");
  display.setCursor(100, 0);
  display.print("P"); display.print(screenPage + 1);

  if (!linkEstablished) {
    display.setCursor(0, 25);
    display.print("Waiting for CRSF...");
    digitalWrite(BUILTIN_LED, HIGH);
  } else {
    digitalWrite(BUILTIN_LED, LOW);
    switch (screenPage) {
      case 0:
        display.setCursor(0, 12); display.print("RSSI: "); display.print(linkStats.uplink_rssi_1); display.print(" dBm");
        display.setCursor(0, 24); display.print("LQ:   "); display.print(linkStats.uplink_link_quality); display.print("%");
        display.setCursor(0, 36);
        if (linkStats.uplink_tx_power < TX_POWER_COUNT) {
          display.print("PWR: "); display.print(txPowerTable[linkStats.uplink_tx_power]); display.print("mW");
        } else {
          display.print("PWR: ? mW");
        }
        display.setCursor(0, 48); display.print("SNR:  "); display.print(linkStats.uplink_snr); display.print(" dB");
        break;

      case 1: // Channels 1–8 (2 columns, 4 rows)
        for (int i = 0; i < 8; i++) {
          int x = (i % 2) * 64;  // 2 columns of 64 pixels
          int y = 12 + (i / 2) * 12;  // Rows with 12-pixel spacing
          display.setCursor(x, y);
          display.printf("C%02d:%4d", i+1, channels[i]);
        }
        break;

      case 2: // Channels 9–16 (2 columns, 4 rows)
        for (int i = 8; i < 16; i++) {
          int x = ((i-8) % 2) * 64;  // 2 columns
          int y = 12 + ((i-8) / 2) * 12;  // Rows with 12-pixel spacing
          display.setCursor(x, y);
          display.printf("C%02d:%4d", i+1, channels[i]);
        }
        break;

      case 3:
        display.setCursor(0, 12); display.print("Ant:  "); display.print(linkStats.active_antenna);
        display.setCursor(0, 24); display.print("Mode: "); display.print(linkStats.rf_mode);
        display.setCursor(0, 36); display.print("D-RSSI:"); display.print(linkStats.downlink_rssi);
        display.setCursor(0, 48); display.print("D-LQ:  "); display.print(linkStats.downlink_link_quality); display.print("%");
        break;
    }
  }
  display.display(); // Always at the end
}

void updateNeoPixel() {
  uint32_t color;
  if (currentMode == MODE_PASSTHROUGH) {
    color = pixel.Color(0, 0, 255);
  } else if (!linkEstablished) {
    color = (millis() / 200 % 2) ? pixel.Color(255, 0, 0) : 0;
  } else if (linkStats.uplink_link_quality > 90) {
    color = pixel.Color(0, 255, 0);
  } else if (linkStats.uplink_link_quality > 50) {
    color = pixel.Color(255, 165, 0);
  } else {
    color = pixel.Color(255, 0, 0);
  }
  pixel.setPixelColor(0, color);
  pixel.show();
}

// ==================== SETUP & LOOP ===========
void setup() {
  set_sys_clock_khz(200000, true);  // 200 MHz

  // Initialize channels to center
  for (int i = 0; i < 16; i++) channels[i] = 1024;

  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, HIGH);

  Wire.setSDA(OLED_SDA_PIN);
  Wire.setSCL(OLED_SCL_PIN);
  Wire.begin();
  Wire.setClock(400000);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  pixel.begin();
  pixel.setBrightness(80);

  usb_hid.setPollInterval(1);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  Serial1.setRX(CRSF_RX_PIN);
  Serial1.setTX(CRSF_TX_PIN);
  Serial1.begin(CRSF_BAUD);
}

void loop() {
  handleButton();

  // Always parse CRSF
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (currentMode == MODE_PASSTHROUGH) Serial.write(b);
    processCrsfByte(b);
  }

  // Switch page on channel 10 (>1500 momentary)
  if (linkEstablished) {
    bool channel10High = (channels[9] > 1500);  // Channel 10 = channels[9]
    if (channel10High && !lastChannel10High) {
      screenPage = (screenPage + 1) % MAX_PAGES;
    }
    lastChannel10High = channel10High;
  }

  // Switch mode on channel 9
  if (linkEstablished && channels[CH_PASSTHROUGH_TOGGLE - 1] > 1500) {
    currentMode = MODE_PASSTHROUGH;
  } else {
    currentMode = MODE_GAMEPAD;
  }

  if (currentMode == MODE_PASSTHROUGH) {
    while (Serial.available()) {
      Serial1.write(Serial.read());
    }
  } else {
    // Gamepad
    gpReport.x  = mapAxis(channels[0]);
    gpReport.y  = mapAxis(channels[1]);
    gpReport.z  = mapThrottle(channels[2]);
    gpReport.rz = mapAxis(channels[3]);
    gpReport.rx = mapAxis(channels[4]);
    gpReport.ry = mapAxis(channels[5]);
    gpReport.hat = 8;

    gpReport.buttons = 0;
    for (int i = 0; i < 10; i++) {
      if (channels[6 + i] > 1500) {
        gpReport.buttons |= (1UL << i);
      }
    }

    if (usb_hid.ready()) {
      usb_hid.sendReport(1, &gpReport, sizeof(gpReport));
    }
  }

  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate >= 150) {
    drawScreen();
    updateNeoPixel();
    lastUpdate = millis();
  }
}
