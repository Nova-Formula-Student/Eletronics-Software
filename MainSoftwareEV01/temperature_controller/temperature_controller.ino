// =============================================================
//  FS TEMPERATURE CONTROLLER — Motor & Inverter
//
//  Analog:  A0=Motor NTC,  A1=Inverter NTC  (KTY 81-210)
//  CAN RX:  0xA1 M161 → motor hot spot    bytes [6:7] int16 x0.1°C
//           0xA2 M162 → inverter hot spot bytes [2:3] int16 x0.1°C
//           0x130     → config from RPi
//                       [0] motor threshold °C
//                       [1] inverter threshold °C
//                       [2] force bitmask (bit0=motor, bit1=inv)
//  CAN TX:  0x120     → status to RPi
//  Fans:    D5=Motor,  D6=Inverter
// =============================================================

#include <SPI.h>
#include <mcp2515.h>
#include <avr/wdt.h>

// ── Pins ─────────────────────────────────────────────────────
const uint8_t PIN_ANALOG[2] = { A0, A1 };
const uint8_t PIN_FAN[2]    = { 5,  6  };
const uint8_t PIN_CS_CAN    = 10;

MCP2515 can(PIN_CS_CAN);

// ── Voltage divider ──────────────────────────────────────────
const float   R_SERIES = 2000.0;
const float   V_REF    = 5.0;
const int     ADC_MAX  = 1023;

// ── KTY 81-210 lookup (-50 to +150 degC, 10 degC steps) ─────
const int     KTY_TEMP_MIN  = -50;
const int     KTY_TEMP_STEP = 10;
const uint8_t KTY_POINTS    = 21;
const float   KTY_R[KTY_POINTS] = {
  1030, 1135, 1247, 1367, 1495,
  1630, 1772, 1922, 2080, 2245,
  2417, 2597, 2785, 2980, 3182,
  3392, 3607, 3817, 4008, 4166,
  4280
};

// ── CAN IDs ───────────────────────────────────────────────────
const uint32_t CAN_ID_M161 = 0xA1;
const uint32_t CAN_ID_M162 = 0xA2;
const uint32_t CAN_ID_TX   = 0x120;
const uint32_t CAN_ID_CFG  = 0x130;

// ── Channel indices ───────────────────────────────────────────
const uint8_t CH_MOTOR = 0;
const uint8_t CH_INV   = 1;
const uint8_t NUM_CH   = 2;

// ── State ─────────────────────────────────────────────────────
float   ntc_temp[NUM_CH]     = { -99.0, -99.0 };
float   hot_spot[NUM_CH]     = { -99.0, -99.0 };
bool    fan_state[NUM_CH]    = { false, false };
bool    fan_forced[NUM_CH]   = { false, false };  // set by RPi via 0x130
float   fan_threshold[NUM_CH] = { 50.0, 50.0 };
uint8_t fault_mask           = 0xFF;

const float HYSTERESIS = 5.0;

// ── Timing ────────────────────────────────────────────────────
const uint32_t LOOP_PERIOD   = 50;
const uint32_t CAN_TX_PERIOD = 100;
uint32_t last_loop_ms = 0;
uint32_t last_can_tx  = 0;

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < NUM_CH; i++) {
    pinMode(PIN_FAN[i], OUTPUT);
    digitalWrite(PIN_FAN[i], LOW);
  }
  can.reset();
  if (can.setBitrate(CAN_500KBPS, MCP_8MHZ) != MCP2515::ERROR_OK) {
    Serial.println(F("CAN FAIL")); while (1);
  }
  can.setNormalMode();
  wdt_enable(WDTO_250MS);
  Serial.println(F("=== Temp Controller ready ==="));
}

// ─────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();
  if ((now - last_loop_ms) < LOOP_PERIOD) return;
  last_loop_ms = now;
  wdt_reset();

  // 1. Read NTC sensors
  fault_mask = 0;
  for (uint8_t i = 0; i < NUM_CH; i++) {
    ntc_temp[i] = readKTY(PIN_ANALOG[i]);
    if (ntc_temp[i] < -90) fault_mask |= (1 << i);
  }

  // 2. Poll CAN
  pollCAN();

  // 3. Fan control
  for (uint8_t i = 0; i < NUM_CH; i++) {
    // Force-on overrides everything — fan ON regardless of temp
    if (fan_forced[i]) {
      fan_state[i] = true;
      digitalWrite(PIN_FAN[i], HIGH);
      continue;
    }

    // Both sources unknown → fail safe ON
    float ntc = ntc_temp[i];
    float hot = hot_spot[i];
    if (ntc < -90 && hot < -90) {
      fan_state[i] = true;
      digitalWrite(PIN_FAN[i], HIGH);
      continue;
    }

    // Use worst of the two valid sources
    float temp;
    if      (ntc < -90) temp = hot;
    else if (hot < -90) temp = ntc;
    else                temp = max(ntc, hot);

    if (!fan_state[i] && temp >= fan_threshold[i]) {
      fan_state[i] = true;
      digitalWrite(PIN_FAN[i], HIGH);
    } else if (fan_state[i] && temp < (fan_threshold[i] - HYSTERESIS)) {
      fan_state[i] = false;
      digitalWrite(PIN_FAN[i], LOW);
    }
  }

  // 4. CAN TX
  if ((now - last_can_tx) >= CAN_TX_PERIOD) {
    last_can_tx = now;
    sendStatus();
    printDebug();
  }
}

// ─────────────────────────────────────────────────────────────
float readKTY(uint8_t pin) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) sum += analogRead(pin);
  float v = (float)(sum >> 3) / ADC_MAX * V_REF;
  if (v <= 0.01 || v >= (V_REF - 0.01)) return -99.0;
  float r = R_SERIES * v / (V_REF - v);
  return resistanceToTemp(r);
}

float resistanceToTemp(float r) {
  if (r <= KTY_R[0]) return (float)KTY_TEMP_MIN;
  if (r >= KTY_R[KTY_POINTS - 1])
    return (float)(KTY_TEMP_MIN + KTY_TEMP_STEP * (KTY_POINTS - 1));
  for (uint8_t i = 0; i < KTY_POINTS - 1; i++) {
    if (r >= KTY_R[i] && r < KTY_R[i + 1]) {
      float t_lo = KTY_TEMP_MIN + (float)i * KTY_TEMP_STEP;
      float frac = (r - KTY_R[i]) / (KTY_R[i + 1] - KTY_R[i]);
      return t_lo + frac * (float)KTY_TEMP_STEP;
    }
  }
  return -99.0;
}

// ─────────────────────────────────────────────────────────────
void pollCAN() {
  struct can_frame frame;
  while (can.readMessage(&frame) == MCP2515::ERROR_OK) {

    // M161: motor hot spot — bytes [6:7] int16 LE x0.1
    if (frame.can_id == CAN_ID_M161 && frame.can_dlc >= 8) {
      int16_t raw = (int16_t)(frame.data[6] | (frame.data[7] << 8));
      hot_spot[CH_MOTOR] = raw * 0.1f;
    }

    // M162: inverter hot spot — bytes [2:3] int16 LE x0.1
    else if (frame.can_id == CAN_ID_M162 && frame.can_dlc >= 4) {
      int16_t raw = (int16_t)(frame.data[2] | (frame.data[3] << 8));
      hot_spot[CH_INV] = raw * 0.1f;
    }

    // 0x130: config from RPi
    //   [0] motor threshold °C
    //   [1] inverter threshold °C
    //   [2] force bitmask (bit0=motor, bit1=inv)
    else if (frame.can_id == CAN_ID_CFG && frame.can_dlc >= 3) {
      for (uint8_t i = 0; i < NUM_CH; i++) {
        uint8_t v = frame.data[i];
        if (v >= 20 && v <= 150) fan_threshold[i] = (float)v;
        fan_forced[i] = (frame.data[2] >> i) & 1;
      }
      Serial.print(F("CFG: Motor thresh="));
      Serial.print(fan_threshold[CH_MOTOR], 0);
      Serial.print(F("C forced="));
      Serial.print(fan_forced[CH_MOTOR]);
      Serial.print(F("  Inv thresh="));
      Serial.print(fan_threshold[CH_INV], 0);
      Serial.print(F("C forced="));
      Serial.println(fan_forced[CH_INV]);
    }
  }
}

// ─────────────────────────────────────────────────────────────
// 0x120 status → RPi
// [0] NTC motor    +50
// [1] NTC inverter +50
// [2] hot spot motor    +50
// [3] hot spot inverter +50
// [4] fan bitmask  (bit0=motor, bit1=inv)
// [5] fault bitmask
// [6] force bitmask (mirrors what RPi sent, for confirmation)
// [7] reserved
void sendStatus() {
  struct can_frame frame;
  frame.can_id  = CAN_ID_TX;
  frame.can_dlc = 8;

  auto enc = [](float t) -> uint8_t {
    return (uint8_t)constrain((int)(t + 50), 0, 255);
  };

  frame.data[0] = enc(ntc_temp[CH_MOTOR]);
  frame.data[1] = enc(ntc_temp[CH_INV]);
  frame.data[2] = enc(hot_spot[CH_MOTOR]);
  frame.data[3] = enc(hot_spot[CH_INV]);

  uint8_t fan_mask   = 0;
  uint8_t force_mask = 0;
  for (uint8_t i = 0; i < NUM_CH; i++) {
    if (fan_state[i])  fan_mask   |= (1 << i);
    if (fan_forced[i]) force_mask |= (1 << i);
  }
  frame.data[4] = fan_mask;
  frame.data[5] = fault_mask;
  frame.data[6] = force_mask;
  frame.data[7] = 0x00;

  can.sendMessage(&frame);
}

// ─────────────────────────────────────────────────────────────
void printDebug() {
  for (uint8_t i = 0; i < NUM_CH; i++) {
    Serial.print(i == CH_MOTOR ? F("Motor  ") : F("Inv    "));
    Serial.print(F("NTC="));    Serial.print(ntc_temp[i], 1);
    Serial.print(F("C  Hot="));  Serial.print(hot_spot[i], 1);
    Serial.print(F("C  Fan="));  Serial.print(fan_state[i]  ? F("ON")     : F("off"));
    Serial.print(F("  Force=")); Serial.print(fan_forced[i] ? F("FORCED") : F("auto"));
    Serial.print(F("  Thr="));   Serial.print(fan_threshold[i], 0);
    Serial.println(F("C"));
  }
  Serial.println(F("---"));
}
