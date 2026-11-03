/**
 * Touch / I2C diagnostic — Waveshare ESP32-S3-Touch-LCD-2.
 *
 * Definitively tests whether the CST816D touch chip responds. Prints:
 *   1. A full I2C bus scan (0x08..0x77) — which addresses ACK.
 *   2. QMI8658 IMU @0x6B WHO_AM_I (reg 0x00) — known-good, proves the bus works.
 *   3. CST816 @0x15 chip-id (reg 0xA7, expect 0xB6), retried continuously.
 *   4. Live touch polling: touch-number reg (0x02) every 200 ms. TOUCH THE
 *      SCREEN — if the chip is alive, 0x15 will ACK and touch-num becomes >0.
 *
 * Shared I2C bus: SDA=48, SCL=47, 100 kHz (matches main project pinout.h).
 */
#include <Arduino.h>
#include <Wire.h>

#define I2C_SDA 48
#define I2C_SCL 47
#define I2C_HZ  100000

#define CST816_ADDR    0x15
#define CST816_ID_REG  0xA7
#define CST816_ID_EXP  0xB6
#define CST816_NUM_REG 0x02

#define IMU_ADDR       0x6B
#define IMU_WHOAMI_REG 0x00   // QMI8658 WHO_AM_I (main project reads 0x00, = 0x05)

static char s_summary[160];

static bool readReg(uint8_t addr, uint8_t reg, uint8_t* out, uint8_t len = 1) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return false;
    uint8_t got = Wire.requestFrom(addr, len);
    if (got < len) return false;
    for (uint8_t i = 0; i < len; i++) out[i] = Wire.read();
    return true;
}

static void busScan() {
    Serial.println("[I2C] scanning 0x08..0x77 ...");
    int found = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C]   ACK at 0x%02X%s\n", a,
                          a == CST816_ADDR ? "  <-- CST816 touch" :
                          a == IMU_ADDR    ? "  <-- QMI8658 IMU"  : "");
            found++;
        }
    }
    Serial.printf("[I2C] scan done, %d device(s)\n", found);
}

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n=== Touch / I2C diagnostic ===");
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_HZ);
    delay(50);

    busScan();

    // IMU control: proves the bus/pull-ups/speed are healthy.
    uint8_t who = 0;
    bool imuok = readReg(IMU_ADDR, IMU_WHOAMI_REG, &who);
    if (imuok) Serial.printf("[IMU ] QMI8658 @0x6B WHO_AM_I=0x%02X (bus OK)\n", who);
    else       Serial.println("[IMU ] QMI8658 @0x6B NO RESPONSE (bus problem!)");

    // CST816 chip id, a few immediate attempts.
    uint8_t id = 0;
    bool ok = false;
    for (int k = 0; k < 5 && !ok; k++) {
        ok = readReg(CST816_ADDR, CST816_ID_REG, &id);
        if (!ok) delay(50);
    }
    if (ok) Serial.printf("[TOUCH] CST816 @0x15 id=0x%02X (expect 0x%02X)\n", id, CST816_ID_EXP);
    else    Serial.println("[TOUCH] CST816 @0x15 NO RESPONSE");

    snprintf(s_summary, sizeof(s_summary),
             "[VERDICT] IMU@0x6B=%s(0x%02X) | CST816@0x15=%s(0x%02X)",
             imuok ? "OK" : "FAIL", who,
             ok ? "OK" : "NO-RESPONSE", id);

    Serial.println("[TOUCH] Now polling — TOUCH THE SCREEN to see if 0x15 wakes up");
}

void loop() {
    static uint32_t n = 0;
    static uint32_t lastSummary = 0;
    if (millis() - lastSummary > 5000) {
        lastSummary = millis();
        Serial.println(s_summary);   // reprint scan/IMU verdict periodically
    }
    uint8_t id = 0, num = 0;
    bool idok  = readReg(CST816_ADDR, CST816_ID_REG, &id);
    bool numok = readReg(CST816_ADDR, CST816_NUM_REG, &num);
    Serial.printf("[%4lu] 0x15 idok=%d id=0x%02X | numok=%d touchNum=%d\n",
                  n++, idok, id, numok, num);
    delay(200);
}
