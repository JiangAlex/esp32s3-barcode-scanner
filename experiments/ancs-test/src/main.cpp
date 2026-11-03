/**
 * ANCS LCD test — Redmine #104.
 *
 * Standalone: ESP32-S3 acts as a BLE *client* and subscribes to the iPhone's
 * Apple Notification Center Service (ANCS). Incoming notifications are printed
 * over serial AND rendered on the 2.8" ST7789T3 LCD (category / title / message,
 * Traditional-Chinese CJK font). Validates "ANCS + LVGL LCD" coexisting before
 * integrating into the main firmware.
 *
 * BLE based on NimBLE-Arduino 2.5.1 examples/ANCS. Display is a self-contained
 * minimal LovyanGFX+LVGL setup (src/display_test.*, src/ui_notify.*).
 */

#include <Arduino.h>
#include "NimBLEDevice.h"
#include <esp_heap_caps.h>

#include "display_test.h"
#include "ui_notify.h"

static NimBLEUUID ancsServiceUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static NimBLEUUID notificationSourceUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static NimBLEUUID controlPointUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static NimBLEUUID dataSourceUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

static NimBLEClient* pClient = nullptr;
static bool          subscribed = false;

static uint8_t latestUID[4];
static volatile bool pendingNotification = false;
static char s_lastCategory[24] = "";  // set by notifSourceCb, shown by dataSourceCb

static const char* categoryName(uint8_t c) {
    switch (c) {
        case 0:  return "Other";
        case 1:  return "IncomingCall";
        case 2:  return "MissedCall";
        case 3:  return "Voicemail";
        case 4:  return "Social";       // iMessage / LINE / etc.
        case 5:  return "Schedule";
        case 6:  return "Email";
        case 7:  return "News";
        case 8:  return "Health";
        case 9:  return "Business";
        case 10: return "Location";
        case 11: return "Entertainment";
        default: return "?";
    }
}

static void printInternalHeap(const char* tag) {
    size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t freePs  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    Serial.printf("[HEAP] %s internal=%u  psram=%u\n", tag, (unsigned)freeInt, (unsigned)freePs);
}

/**
 * Data Source payload (GetNotificationAttributes response):
 *   [0] cmd(0), [1..4] UID, then repeating [attrId][len LE 2B][value].
 * Collect Title (1) and Message (3) and push both to the LCD in one post.
 */
static volatile bool printingDetail = false;

static void dataSourceCb(NimBLERemoteCharacteristic*, uint8_t* p, size_t len, bool) {
    if (len < 5) return;
    printingDetail = true;

    char title[128] = "";
    char message[192] = "";
    char when[16] = "";     // ANCS Date → "HH:MM"

    size_t i = 5;  // skip cmd + 4-byte UID
    Serial.println("---- notification detail ----");
    while (i + 3 <= len) {
        uint8_t  attrId = p[i];
        uint16_t aLen   = p[i + 1] | (p[i + 2] << 8);
        i += 3;
        if (i + aLen > len) break;
        const char* name = attrId == 1 ? "Title" : attrId == 3 ? "Message"
                         : attrId == 5 ? "Date"  : "Attr";
        Serial.printf("[ANCS] %s (%u bytes): ", name, (unsigned)aLen);
        Serial.write(&p[i], aLen);
        Serial.println();
        // Copy into local NUL-terminated buffer for the LCD.
        if (attrId == 1) {
            uint16_t n = aLen < sizeof(title) - 1 ? aLen : sizeof(title) - 1;
            memcpy(title, &p[i], n); title[n] = 0;
        } else if (attrId == 3) {
            uint16_t n = aLen < sizeof(message) - 1 ? aLen : sizeof(message) - 1;
            memcpy(message, &p[i], n); message[n] = 0;
        } else if (attrId == 5) {
            // ANCS Date format: "yyyyMMdd'T'HHmmSS" (e.g. 20261028T173045).
            // Extract HH:MM for the card.
            char raw[20] = "";
            uint16_t n = aLen < sizeof(raw) - 1 ? aLen : sizeof(raw) - 1;
            memcpy(raw, &p[i], n); raw[n] = 0;
            char* t = strchr(raw, 'T');
            if (t && strlen(t) >= 5) {
                when[0] = t[1]; when[1] = t[2]; when[2] = ':';
                when[3] = t[3]; when[4] = t[4]; when[5] = 0;
            }
        }
        i += aLen;
    }
    Serial.println("-----------------------------");

    // Push to LCD (cross-task safe). Shown on the LVGL task via ui_notify_drain().
    ui_notify_post(s_lastCategory, when, title, message);

    printInternalHeap("after-detail");
    printingDetail = false;
}

static void notifSourceCb(NimBLERemoteCharacteristic*, uint8_t* p, size_t len, bool) {
    if (len < 8) return;
    uint8_t event = p[0];
    uint8_t cat   = p[2];
    if (event == 0) {  // added
        memcpy(latestUID, &p[4], 4);
        strncpy(s_lastCategory, categoryName(cat), sizeof(s_lastCategory) - 1);
        s_lastCategory[sizeof(s_lastCategory) - 1] = 0;
        Serial.printf("[ANCS] New notification — category=%s\n", s_lastCategory);
        ui_notify_set_status("收到通知 查詢內容");
        pendingNotification = true;
    } else if (event == 2) {  // removed
        Serial.println("[ANCS] Notification removed");
    }
}

class ClientStateCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
        Serial.printf("[BLE] iPhone connected: %s\n", info.getAddress().toString().c_str());
        pClient = NimBLEDevice::getServer()->getClient(info);
        subscribed = false;
        ui_notify_set_status("iPhone 已連線");
        printInternalHeap("on-connect");
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
        Serial.printf("[BLE] iPhone disconnected (reason=%d)\n", reason);
        pClient = nullptr;
        subscribed = false;
        ui_notify_set_status("已斷線 重新廣播");
    }
} stateCb;

// ─── LVGL task (Core 1): drain pending UI updates + pump LVGL ────────────────
static void lvgl_task(void*) {
    for (;;) {
        ui_notify_drain();
        display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ANCS LCD test (Redmine #104) ===");
    printInternalHeap("boot");

    // Display first (LCD occupies the FSPI bus).
    display_init();
    ui_notify_init();
    ui_notify_set_status("廣播中: SoftSnail-ANCS");
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, nullptr, 2, nullptr, 1);

    NimBLEDevice::init("SoftSnail-ANCS");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(true, true, true);          // bond, MITM, SC
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);

    NimBLEServer* s = NimBLEDevice::createServer();
    s->setCallbacks(&stateCb);
    s->advertiseOnDisconnect(true);

    NimBLEAdvertising* adv = s->getAdvertising();
    NimBLEAdvertisementData data{};
    data.setFlags(0x06);
    data.setName("SoftSnail-ANCS");
    data.addServiceUUID(ancsServiceUUID);
    adv->setAdvertisementData(data);
    adv->start();

    Serial.println("[BLE] Advertising as \"SoftSnail-ANCS\"");
    printInternalHeap("advertising");
}

void loop() {
    if (pClient && pClient->isConnected() && !subscribed) {
        auto svc = pClient->getService(ancsServiceUUID);
        if (!svc) { delay(200); return; }
        auto ns = svc->getCharacteristic(notificationSourceUUID);
        auto cp = svc->getCharacteristic(controlPointUUID);
        auto ds = svc->getCharacteristic(dataSourceUUID);
        if (!ns || !cp || !ds) { delay(200); return; }
        ds->subscribe(true, dataSourceCb);
        ns->subscribe(true, notifSourceCb);
        subscribed = true;
        Serial.println("[ANCS] Subscribed");
        ui_notify_set_status("已訂閱 等待通知");
        printInternalHeap("subscribed");
    }

    if (subscribed && pendingNotification && pClient && pClient->isConnected()) {
        pendingNotification = false;
        auto svc = pClient->getService(ancsServiceUUID);
        auto cp  = svc ? svc->getCharacteristic(controlPointUUID) : nullptr;
        if (cp) {
            // GetNotificationAttributes in ONE request: Title(1,max64) +
            // Message(3,max128) + Date(5, no max-length). The Data Source
            // response carries all three attributes together.
            uint8_t req[] = {
                0x00, latestUID[0], latestUID[1], latestUID[2], latestUID[3],
                0x01, 0x40, 0x00,   // Title,   max 0x0040
                0x03, 0x80, 0x00,   // Message, max 0x0080
                0x05                // Date,    no max-length
            };
            cp->writeValue(req, sizeof(req), true);
        }
    }

    static uint32_t last = 0;
    if (!printingDetail && millis() - last > 15000) { last = millis(); printInternalHeap("idle"); }
    delay(10);
}
