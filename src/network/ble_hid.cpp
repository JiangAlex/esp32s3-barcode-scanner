/**
 * @file ble_hid.cpp
 * @brief Thin BLE HID keyboard implementation on NimBLE-Arduino 2.x.
 */

#include "ble_hid.h"
#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_coexist.h>
#include "config/config.h"

// ─── State ────────────────────────────────────────────────────────────────────

static bool               s_started   = false;
static volatile bool      s_connected = false;
static volatile bool      s_stopping  = false;   // true during ble_hid_stop teardown
static NimBLEServer*      s_server    = nullptr;
static NimBLEHIDDevice*   s_hid       = nullptr;
static NimBLECharacteristic* s_input  = nullptr;   // input report (keystrokes)
static ble_hid_stopped_cb_t  s_stopped_cb = nullptr;

void ble_hid_set_stopped_cb(ble_hid_stopped_cb_t cb) { s_stopped_cb = cb; }

// ─── HID report map: standard 101-key boot keyboard, report id 1 ───────────────

static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224) — modifiers
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute) — modifier byte
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x03,  //   Input (Constant) — reserved byte
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array) — 6 key array
    0xC0         // End Collection
};

// Keyboard input report: [modifier, reserved, k1..k6]
static uint8_t s_report[8] = {0};

// ─── Connection callbacks ──────────────────────────────────────────────────────

class HidServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        s_connected = true;
        Serial.println("[BLE] host connected");
    }
    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
        s_connected = false;
        // Do NOT re-advertise if we are tearing the stack down (ble_hid_stop):
        // startAdvertising() on a half-deinited stack dereferences freed objects
        // and panics (PC=0). Only re-advertise on a genuine host-initiated drop.
        if (s_stopping) return;
        Serial.printf("[BLE] host disconnected (reason=%d), re-advertising\n", reason);
        NimBLEDevice::startAdvertising();
    }
};

// NOTE: NimBLEDevice::deinit() deletes the server's callbacks object, so it
// MUST be heap-allocated (a static instance caused a "free target outside heap"
// assert on stop). Allocated with new in ble_hid_begin(); NimBLE owns/frees it.

// ─── ASCII → HID usage code (US layout) ────────────────────────────────────────
// Returns the HID usage code and sets shift=true if the char needs Shift.
// Returns 0 for characters we cannot map.

static uint8_t ascii_to_hid(char c, bool* shift) {
    *shift = false;
    if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
    if (c >= 'A' && c <= 'Z') { *shift = true; return 0x04 + (c - 'A'); }
    if (c >= '1' && c <= '9') return 0x1E + (c - '1');
    if (c == '0') return 0x27;

    switch (c) {
        case ' ':  return 0x2C;
        case '\n': return 0x28;  // Enter
        case '\t': return 0x2B;
        case '-':  return 0x2D;
        case '_':  *shift = true; return 0x2D;
        case '=':  return 0x2E;
        case '+':  *shift = true; return 0x2E;
        case '[':  return 0x2F;
        case '{':  *shift = true; return 0x2F;
        case ']':  return 0x30;
        case '}':  *shift = true; return 0x30;
        case '\\': return 0x31;
        case '|':  *shift = true; return 0x31;
        case ';':  return 0x33;
        case ':':  *shift = true; return 0x33;
        case '\'': return 0x34;
        case '"':  *shift = true; return 0x34;
        case '`':  return 0x35;
        case '~':  *shift = true; return 0x35;
        case ',':  return 0x36;
        case '<':  *shift = true; return 0x36;
        case '.':  return 0x37;
        case '>':  *shift = true; return 0x37;
        case '/':  return 0x38;
        case '?':  *shift = true; return 0x38;
        case '!':  *shift = true; return 0x1E;
        case '@':  *shift = true; return 0x1F;
        case '#':  *shift = true; return 0x20;
        case '$':  *shift = true; return 0x21;
        case '%':  *shift = true; return 0x22;
        case '^':  *shift = true; return 0x23;
        case '&':  *shift = true; return 0x24;
        case '*':  *shift = true; return 0x25;
        case '(':  *shift = true; return 0x26;
        case ')':  *shift = true; return 0x27;
        default:   return 0x00;
    }
}

// Send one key press + release (report id 1 is implicit via characteristic).
static void send_key(uint8_t usage, bool shift) {
    if (!s_input || !s_connected) return;
    s_report[0] = shift ? 0x02 : 0x00;  // left-shift modifier
    s_report[2] = usage;
    s_input->setValue(s_report, sizeof(s_report));
    s_input->notify();
    delay(8);
    // release all keys
    s_report[0] = 0x00;
    s_report[2] = 0x00;
    s_input->setValue(s_report, sizeof(s_report));
    s_input->notify();
    delay(8);
}

// ─── Public API ─────────────────────────────────────────────────────────────

void ble_hid_begin(void) {
    if (s_started) return;

    // Single 2.4GHz radio AND tight internal DRAM: the BLE controller needs
    // ~89 KB of internal DRAM, but with WiFi up only ~93 KB is free, leaving no
    // margin (BLE notify mallocs then fail mid-send). INPUT (barcode-gun) mode
    // Single 2.4GHz radio AND tight internal DRAM: the BLE controller needs
    // ~89 KB of internal DRAM, but with WiFi up only ~93 KB is free, leaving no
    // margin (BLE notify mallocs then fail mid-send). INPUT (barcode-gun) mode
    // does not use WiFi/MQTT, so release WiFi here.
    //
    // Use the Arduino WiFi.mode(WIFI_OFF) — NOT a raw esp_wifi_deinit() — so the
    // Arduino WiFi class state stays in sync. A raw deinit desyncs it and the
    // later WiFi.mode(WIFI_STA)/begin() fails with status=255 (NO_SHIELD),
    // dropping into the config portal. WIFI_OFF frees the driver memory and lets
    // WIFI_STA re-init cleanly (recovery verified).
    WiFi.mode(WIFI_OFF);
    delay(50);
    esp_coex_preference_set(ESP_COEX_PREFER_BT);

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);             // max TX power
    // HID keyboards use Just-Works pairing: no input/output capability so the
    // host does not prompt for a passkey. Without this some hosts stall during
    // encryption negotiation and the HID link never comes up.
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, false, true);  // bond, no MITM, SC

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(new HidServerCallbacks());

    s_hid = new NimBLEHIDDevice(s_server);
    s_input = s_hid->getInputReport(1);   // report id 1

    s_hid->setManufacturer("SoftSnail");
    s_hid->setPnp(0x02, 0x05ac, 0x820a, 0x0001);
    s_hid->setHidInfo(0x00, 0x01);
    s_hid->setReportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    s_hid->startServices();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setName(BLE_DEVICE_NAME);        // REQUIRED in NimBLE 2.x — without this
                                          // the advertisement has no name and the
                                          // device is not discoverable by name.
    adv->setAppearance(0x03C1);           // HID keyboard
    adv->addServiceUUID(s_hid->getHidService()->getUUID());
    adv->enableScanResponse(true);        // put the name in the scan response so
                                          // it fits alongside the service UUID
    bool ok = NimBLEDevice::startAdvertising();
    Serial.printf("[BLE] startAdvertising ok=%d\n", ok);

    s_started = true;
    Serial.printf("[BLE] HID keyboard started, advertising as \"%s\"\n", BLE_DEVICE_NAME);
}

void ble_hid_stop(void) {
    if (!s_started) return;
    s_stopping = true;                 // suppress re-advertise from onDisconnect
    NimBLEDevice::stopAdvertising();
    delay(20);                         // let a pending disconnect callback settle
    NimBLEDevice::deinit(true);   // release controller memory
    s_started   = false;
    s_connected = false;
    s_stopping  = false;
    s_server = nullptr;
    s_hid = nullptr;
    s_input = nullptr;
    Serial.println("[BLE] HID keyboard stopped");
    // Let the app re-establish WiFi (fully released in ble_hid_begin).
    if (s_stopped_cb) s_stopped_cb();
}

bool ble_hid_is_started(void)   { return s_started; }
bool ble_hid_is_connected(void) { return s_connected; }

void ble_hid_type_line(const char* text) {
    if (!text || !s_connected) return;
    for (const char* p = text; *p; p++) {
        bool shift = false;
        uint8_t u = ascii_to_hid(*p, &shift);
        if (u) send_key(u, shift);
    }
    // trailing Enter
    send_key(0x28, false);
}
