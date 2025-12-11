#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// ==================== PINS ====================
#define CRSF_RX_PIN     1
#define CRSF_TX_PIN     0
#define CRSF_BAUD       420000

// ==================== MODES ===================
enum DeviceMode { MODE_GAMEPAD, MODE_PASSTHROUGH };
DeviceMode currentMode = MODE_GAMEPAD;

// Channel 9 value > 1500 triggers Passthrough mode
#define CH_PASSTHROUGH_TOGGLE 9   

// ==================== OBJECTS =================
uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(1)) };
Adafruit_USBD_HID usb_hid;

// Gamepad Report Structure
typedef struct __attribute__((packed)) {
  int8_t  x, y, z, rz, rx, ry;
  uint8_t hat;
  uint32_t buttons;
} gamepad_report_t;
gamepad_report_t gpReport = {0};

// ==================== CRSF ====================
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

// ==================== PARSER ==================
enum ParseState { WAIT_ADDR, WAIT_LEN, WAIT_PAYLOAD };
ParseState parseState = WAIT_ADDR;
uint8_t frameBuf[64];
uint8_t frameLen = 0;
uint8_t framePos = 0;

// ==================== CRC8 ====================
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

// ==================== CRSF PROCESS ============
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

// ==================== MAPPING =================
int8_t mapAxis(uint16_t v) {
  int32_t c = v - 1024;
  return (int8_t)constrain((c * 127L) / 1024L, -127, 127);
}

int8_t mapThrottle(uint16_t v) {
  return (int8_t)constrain(((int32_t)v * 254L / 2047L) - 127, -127, 127);
}

// ==================== SETUP & LOOP ============
void setup() {
  set_sys_clock_khz(200000, true);  // Overclock to 200 MHz

  // Initialize channels to center
  for (int i = 0; i < 16; i++) channels[i] = 1024;

  usb_hid.setPollInterval(1);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  Serial1.setRX(CRSF_RX_PIN);
  Serial1.setTX(CRSF_TX_PIN);
  Serial1.begin(CRSF_BAUD);
}

void loop() {
  // Always parse CRSF input
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (currentMode == MODE_PASSTHROUGH) Serial.write(b);
    processCrsfByte(b);
  }

  // Switch mode based on Channel 9
  if (linkEstablished && channels[CH_PASSTHROUGH_TOGGLE - 1] > 1500) {
    currentMode = MODE_PASSTHROUGH;
  } else {
    currentMode = MODE_GAMEPAD;
  }

  if (currentMode == MODE_PASSTHROUGH) {
    // Forward USB Serial to UART (CRSF)
    while (Serial.available()) {
      Serial1.write(Serial.read());
    }
  } else {
    // Gamepad Mode
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
}