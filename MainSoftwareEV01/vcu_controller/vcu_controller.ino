/**
 * FSE VCU — Vehicle Control Unit
 * ════════════════════════════════════════════════════════════════════
 * CAN is initialized once at startup — if it fails the VCU continues
 * running normally. CAN messages are sent every loop regardless.
 * When the inverter is connected it will start receiving immediately.
 *
 * Hardware:
 *   Arduino Nano Every
 *   MCP2515 on SPI  CS=D10, INT=D9, 8MHz crystal
 *   APPS1 on A0     (inverted: voltage drops on press)
 *   RTD button on A4 (3.3V signal, 10kΩ pull-down to GND)
 *   A5 output       (HIGH = 3.3V source for RTD button)
 *   Buzzer on D3    (active HIGH via TPL7407L)
 *   TSAL green D5   (active LOW relay)
 *   TSAL red D6     (active LOW relay)
 *   Fault LED D7
 *   Ready LED D8
 *   Brake light D2  (HIGH = on, active when throttle = 0%)
 *   HV signal A3    (12V via 10kΩ/4.7kΩ divider)
 *
 * Safety:
 *   FSAE T.4.3   — APPS range check (open/short) → latching fault
 *   FSAE EV.10.4 — RTD buzzer 2 s
 *
 * TEMPORARY (bench/initial testing):
 *   - Single APPS sensor (A0 only)
 *   - Plausibility disabled
 *   - BSE disabled
 *   - Brake light = throttle idle
 *   - RTD no brake requirement
 *   - CAN timeout NOT a latching fault
 *   - MAX_TORQUE_NM = 11.5 Nm (5% of EMRAX 228 peak)
 */

#include <SPI.h>
#include <mcp_can.h>

// ═══════════════════════════════════════════════════════════════════
//  PINS
// ═══════════════════════════════════════════════════════════════════
#define CAN_CS_PIN        10
#define APPS1_PIN         A0
#define HV_SIGNAL_PIN     A3
#define RTD_BUTTON_PIN    A4
#define BUZZER_PIN         3
#define TSAL_GREEN_PIN     5
#define TSAL_RED_PIN       6
#define FAULT_LED_PIN      7
#define READY_LED_PIN      8
#define BRAKE_LIGHT_PIN    2

#define TSAL_BLINK_HZ      3
#define TSAL_BLINK_MS      (1000 / TSAL_BLINK_HZ / 2)

// ═══════════════════════════════════════════════════════════════════
//  CAN IDs
// ═══════════════════════════════════════════════════════════════════
#define CAN_ID_CMD        0xC0
#define CAN_ID_STATE      0xAA
#define CAN_ID_FAULT      0xAB

// ═══════════════════════════════════════════════════════════════════
//  APPS CALIBRATION (inverted sensor — voltage drops on press)
// ═══════════════════════════════════════════════════════════════════
#define APPS1_MIN         515   // resting → 0%
#define APPS1_MAX         460   // full press → 100%

#define APPS_RAW_LOW_LIMIT    10
#define APPS_RAW_HIGH_LIMIT 1010

// ═══════════════════════════════════════════════════════════════════
//  RTD
// ═══════════════════════════════════════════════════════════════════
#define RTD_BUZZER_MS     2000UL

// ═══════════════════════════════════════════════════════════════════
//  TORQUE (5% of EMRAX 228 peak 230 Nm)
// ═══════════════════════════════════════════════════════════════════
#define MAX_TORQUE_NM     11.5f
#define TORQUE_RAMP_RATE   0.25f  // Nm per 10ms
#define TORQUE_SCALE          10  // DBC: raw = Nm × 10

// ═══════════════════════════════════════════════════════════════════
//  VCU STATE MACHINE
// ═══════════════════════════════════════════════════════════════════
enum VCU_State : uint8_t {
    VCU_RTD_WAIT = 0,
    VCU_RTD_BUZZ = 1,
    VCU_RUNNING  = 2,
    VCU_FAULT    = 3
};

#define FAULT_NONE       0x00
#define FAULT_APPS_RANGE 0x02

// ═══════════════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════════════
MCP_CAN CAN(CAN_CS_PIN);
bool    canReady  = false;   // true if MCP2515 init succeeded

VCU_State vcuState   = VCU_RTD_WAIT;
uint8_t   faultFlags = FAULT_NONE;
uint8_t   rollingCtr = 0;

// Inverter feedback (updated from CAN if connected)
uint8_t   vsmState   = 0;
bool      invEnabled = false;

float     apps1Pct   = 0.0f;
float     torqueOut  = 0.0f;
float     torquePrev = 0.0f;
int       raw1       = 0;

bool      rtdActive    = false;
uint32_t  rtdBuzzStart = 0;

// ═══════════════════════════════════════════════════════════════════
//  PROTOTYPES
// ═══════════════════════════════════════════════════════════════════
void    sendCommandMessage(float torqueNm, bool enable, bool forward);
void    handleCAN();
void    updateTSAL();
float   applyDeadzone(int raw, int rawMin, int rawMax);
int16_t toRaw(float nm);

// ═══════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════
void setup() {
    // Buzzer LOW before anything else to prevent boot glitch
    pinMode(BUZZER_PIN,      OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    pinMode(FAULT_LED_PIN,   OUTPUT);
    pinMode(READY_LED_PIN,   OUTPUT);
    pinMode(TSAL_GREEN_PIN,  OUTPUT);
    pinMode(TSAL_RED_PIN,    OUTPUT);
    pinMode(BRAKE_LIGHT_PIN, OUTPUT);
    pinMode(RTD_BUTTON_PIN,  INPUT);
    pinMode(A5,              OUTPUT);

    digitalWrite(FAULT_LED_PIN,  LOW);
    digitalWrite(READY_LED_PIN,  LOW);
    digitalWrite(TSAL_GREEN_PIN, LOW);   // green on (LV active)
    digitalWrite(TSAL_RED_PIN,   HIGH);  // red off
    digitalWrite(BRAKE_LIGHT_PIN,LOW);
    digitalWrite(A5,             HIGH);  // 3.3V source for RTD button

    Serial.begin(115200);
    Serial.println(F("═══════════════════════════════"));
    Serial.println(F("  FSE VCU — Starting up"));
    Serial.println(F("═══════════════════════════════"));

    // Non-blocking CAN init — VCU runs even if CAN fails
    if (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK) {
        CAN.setMode(MCP_NORMAL);
        canReady = true;
        Serial.println(F("CAN ready at 500 kbps"));
    } else {
        canReady = false;
        Serial.println(F("CAN init failed — running without CAN"));
        Serial.println(F("Connect MCP2515 and power cycle to enable CAN"));
    }

    Serial.println(F("Press RTD button on A4 to activate"));

    if (canReady) {
        uint8_t wake[8] = {0,0,0,0,0,0,0,0};
        CAN.sendMsgBuf(CAN_ID_CMD, 0, 8, wake);
        Serial.println(F("CAN wake frame sent"));
    }
}

// ═══════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════
void loop() {
    // Drain CAN RX if available
    if (canReady) handleCAN();

    static uint32_t lastCmdMs = 0;
    if (millis() - lastCmdMs < 10) return;
    lastCmdMs = millis();

    // ── Sensor read ───────────────────────────────────────────────
    raw1     = analogRead(APPS1_PIN);
    apps1Pct = applyDeadzone(raw1, APPS1_MIN, APPS1_MAX);

    // ── Range check ───────────────────────────────────────────────
    if (raw1 < APPS_RAW_LOW_LIMIT || raw1 > APPS_RAW_HIGH_LIMIT) {
        if (!(faultFlags & FAULT_APPS_RANGE)) {
            Serial.print(F("RANGE FAULT: APPS1 raw=")); Serial.println(raw1);
        }
        faultFlags |= FAULT_APPS_RANGE;
    }

    bool latchingFault = (faultFlags & FAULT_APPS_RANGE);
    if (latchingFault && vcuState != VCU_FAULT) {
        vcuState  = VCU_FAULT;
        rtdActive = false;
        digitalWrite(BUZZER_PIN,    LOW);
        digitalWrite(READY_LED_PIN, LOW);
        Serial.println(F("LATCHING FAULT — power cycle to reset"));
    }

    bool rtdBtn = digitalRead(RTD_BUTTON_PIN);

    // ── State machine ─────────────────────────────────────────────
    switch (vcuState) {

        case VCU_RTD_WAIT: {
            // Send zero torque, inverter disabled
            sendCommandMessage(0.0f, false, true);

            static uint32_t lastLog = 0;
            if (millis() - lastLog >= 500) {
                lastLog = millis();
                Serial.print(F("[RTD_WAIT] btn="));
                Serial.print(rtdBtn ? F("PRESSED") : F("open"));
                Serial.print(F(" APPS1="));
                Serial.print(apps1Pct * 100.0f, 1);
                Serial.print(F("% CAN="));
                Serial.println(canReady ? F("OK") : F("NO"));
            }
            if (rtdBtn) {
                Serial.println(F("RTD accepted — buzzer ON (2 s)"));
                rtdBuzzStart = millis();
                digitalWrite(BUZZER_PIN, HIGH);
                vcuState = VCU_RTD_BUZZ;
            }
            break;
        }

        case VCU_RTD_BUZZ: {
            // Zero torque during buzz
            sendCommandMessage(0.0f, false, true);

            static uint32_t lastBuzzLog = 0;
            if (millis() - lastBuzzLog >= 200) {
                lastBuzzLog = millis();
                uint32_t elapsed   = millis() - rtdBuzzStart;
                uint32_t remaining = (elapsed < RTD_BUZZER_MS)
                                     ? RTD_BUZZER_MS - elapsed : 0;
                Serial.print(F("[RTD_BUZZ] remaining: "));
                Serial.print(remaining);
                Serial.println(F(" ms"));
            }
            if (millis() - rtdBuzzStart >= RTD_BUZZER_MS) {
                digitalWrite(BUZZER_PIN, LOW);
                rtdActive = true;
                vcuState  = VCU_RUNNING;
                digitalWrite(READY_LED_PIN, HIGH);
                Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
                Serial.println(F("  RTD COMPLETE — RUNNING"));
                Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
            }
            break;
        }

        case VCU_RUNNING: {
            if (rtdActive && !latchingFault) {
                float target = apps1Pct * MAX_TORQUE_NM;
                float delta  = target - torquePrev;
                if (delta >  TORQUE_RAMP_RATE) delta =  TORQUE_RAMP_RATE;
                if (delta < -TORQUE_RAMP_RATE) delta = -TORQUE_RAMP_RATE;
                torqueOut  = torquePrev + delta;
                torquePrev = torqueOut;
            } else {
                torqueOut = 0.0f;
            }
            sendCommandMessage(torqueOut, true, true);
            break;
        }

        case VCU_FAULT:
            torqueOut = 0.0f;
            sendCommandMessage(0.0f, false, true);
            break;
    }

    // ── Dashboard telemetry 0x101 ─────────────────────────────────
    static uint32_t lastTelMs = 0;
    static uint8_t  pktCtr    = 0;
    if (millis() - lastTelMs >= 100) {
        lastTelMs = millis();
        pktCtr    = (pktCtr + 1) & 0x0F;
        uint8_t buf[4];
        buf[0] = (uint8_t)(apps1Pct * 255.0f);
        buf[1] = 0;
        buf[2] = faultFlags;
        buf[3] = pktCtr;
        if (canReady) CAN.sendMsgBuf(0x101, 0, 4, buf);
    }

    // ── Brake light ───────────────────────────────────────────────
    digitalWrite(BRAKE_LIGHT_PIN, apps1Pct <= 0.01f ? HIGH : LOW);

    // ── TSAL ──────────────────────────────────────────────────────
    updateTSAL();

    // ── LEDs ──────────────────────────────────────────────────────
    digitalWrite(FAULT_LED_PIN, faultFlags != FAULT_NONE ? HIGH : LOW);

    // ── Status print ──────────────────────────────────────────────
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 200) {
        lastPrint = millis();
        const char* stateStr[] = {"RTD_WAIT","RTD_BUZZ","RUNNING","FAULT"};
        Serial.print(F("VCU:")); Serial.print(stateStr[vcuState]);
        Serial.print(F(" RTD:")); Serial.print(rtdActive ? F("ON") : F("OFF"));
        Serial.print(F(" CAN:")); Serial.print(canReady  ? F("OK") : F("NO"));
        Serial.print(F(" | APPS1:")); Serial.print(apps1Pct * 100.0f, 1);
        Serial.print(F("% | Tq:")); Serial.print(torqueOut, 2);
        Serial.print(F("Nm | VSM:")); Serial.print(vsmState);
        Serial.print(F(" | RAW:")); Serial.print(raw1);
        Serial.print(F(" V:")); Serial.print(raw1 * (5.0f / 1023.0f), 2);
        Serial.print(F("V | F:0x")); Serial.println(faultFlags, HEX);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  handleCAN — read inverter messages
// ═══════════════════════════════════════════════════════════════════
void handleCAN() {
    while (CAN.checkReceive() == CAN_MSGAVAIL) {
        uint8_t  buf[8];
        uint32_t id;
        uint8_t  len;
        if (CAN.readMsgBuf(&id, &len, buf) != CAN_OK) continue;

        switch (id) {
            case CAN_ID_STATE:
                vsmState   = buf[0];
                invEnabled = (buf[6] >> 0) & 0x01;
                if ((buf[6] >> 7) & 0x01)
                    Serial.println(F("WARN: inverter enable lockout"));
                break;

            case CAN_ID_FAULT: {
                uint16_t postLo = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
                uint16_t postHi = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
                uint16_t runLo  = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
                uint16_t runHi  = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
                if (postLo || postHi || runLo || runHi) {
                    Serial.print(F("INV FAULT Post:"));
                    Serial.print(postHi, HEX); Serial.print(':');
                    Serial.print(postLo, HEX);
                    Serial.print(F(" Run:"));
                    Serial.print(runHi,  HEX); Serial.print(':');
                    Serial.println(runLo, HEX);
                }
                break;
            }
            default: break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
//  sendCommandMessage — M192 (0xC0)
// ═══════════════════════════════════════════════════════════════════
void sendCommandMessage(float torqueNm, bool enable, bool forward) {
    if (!canReady) return;
    uint8_t buf[8] = {0};
    int16_t raw    = toRaw(torqueNm);
    buf[0] = raw & 0xFF;
    buf[1] = (raw >> 8) & 0xFF;
    buf[4] = forward ? 0x01 : 0x00;
    rollingCtr = (rollingCtr + 1) & 0x0F;
    buf[5]  = enable ? (1 << 0) : 0;
    buf[5] |= (rollingCtr << 4);
    CAN.sendMsgBuf(CAN_ID_CMD, 0, 8, buf);
}

// ═══════════════════════════════════════════════════════════════════
//  updateTSAL
// ═══════════════════════════════════════════════════════════════════
void updateTSAL() {
    static uint32_t lastBlinkMs = 0;
    static bool     blinkState  = false;
    bool hvOn = digitalRead(HV_SIGNAL_PIN);
    if (hvOn) {
        digitalWrite(TSAL_GREEN_PIN, HIGH);
        if (millis() - lastBlinkMs >= TSAL_BLINK_MS) {
            blinkState  = !blinkState;
            lastBlinkMs = millis();
            digitalWrite(TSAL_RED_PIN, blinkState ? LOW : HIGH);
        }
    } else {
        digitalWrite(TSAL_GREEN_PIN, LOW);
        digitalWrite(TSAL_RED_PIN,   HIGH);
        blinkState = false;
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════
float applyDeadzone(int raw, int rawMin, int rawMax) {
    if (rawMin < rawMax) {
        if (raw <= rawMin) return 0.0f;
        if (raw >= rawMax) return 1.0f;
        return (float)(raw - rawMin) / (float)(rawMax - rawMin);
    } else {
        if (raw >= rawMin) return 0.0f;
        if (raw <= rawMax) return 1.0f;
        return (float)(rawMin - raw) / (float)(rawMin - rawMax);
    }
}

int16_t toRaw(float nm) {
    if (nm >  3276.7f) nm =  3276.7f;
    if (nm < -3276.8f) nm = -3276.8f;
    return (int16_t)(nm * TORQUE_SCALE);
}
