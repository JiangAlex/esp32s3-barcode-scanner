/**
 * @file mqtt_client.cpp
 * @brief MQTT client implementation using PubSubClient
 */

#include "mqtt_client.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config/config.h"
#include "wifi_manager.h"
#include "storage/scan_log.h"

static WiFiClient wifi_client;
static PubSubClient mqtt(wifi_client);
static mqtt_response_cb_t response_callback = nullptr;

// ─── MQTT message callback ──────────────────────────────────────────────────

static void mqtt_message_cb(char* topic, byte* payload, unsigned int length) {
    // Parse response JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);

    if (err) {
        Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
        return;
    }

    const char* status = doc["status"] | "unknown";
    Serial.printf("[MQTT] Response: status=%s\n", status);

    // Call registered callback
    if (response_callback) {
        response_callback(status, doc);
    }
}

// ─── Public functions ───────────────────────────────────────────────────────

// Runtime broker config (defaults to config.h; overridable via the WiFi portal
// through mqtt_set_broker()).
static char     s_broker[64] = MQTT_BROKER;
static uint16_t s_port       = MQTT_PORT;

void mqtt_set_broker(const char* host, uint16_t port) {
    if (host && host[0]) {
        strncpy(s_broker, host, sizeof(s_broker) - 1);
        s_broker[sizeof(s_broker) - 1] = '\0';
    }
    if (port) s_port = port;
    Serial.printf("[MQTT] broker set to %s:%u\n", s_broker, s_port);
}

void mqtt_client_init(void) {
    mqtt.setServer(s_broker, s_port);
    mqtt.setCallback(mqtt_message_cb);
    mqtt.setBufferSize(1024);  // Increase buffer for JSON payloads

    if (wifi_is_connected()) {
        mqtt_reconnect();
    }
}

void mqtt_client_loop(void) {
    if (!mqtt.connected()) {
        // Retry quickly (1.5 s) but only once WiFi is actually up — avoids
        // burning a reconnect attempt while the WiFi driver is still
        // associating (e.g. right after the INPUT-BLE → INPUT-MQTT handoff,
        // where BLE released WiFi and it is re-connecting). This cuts the
        // post-handoff MQTT recovery from ~15-20 s down to ~1-2 s after WiFi.
        static unsigned long last_attempt = 0;
        if (wifi_is_connected() && millis() - last_attempt > 1500) {
            last_attempt = millis();
            mqtt_reconnect();
        }
    }
    mqtt.loop();
}

bool mqtt_is_connected(void) {
    return mqtt.connected();
}

bool mqtt_query_barcode(const char* barcode) {
    if (!mqtt.connected()) {
        Serial.println("[MQTT] Not connected, cannot query");
        return false;
    }

    JsonDocument doc;
    doc["barcode"] = barcode;
    doc["device_id"] = DEVICE_ID;

    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));

    bool ok = mqtt.publish(MQTT_TOPIC_QUERY, buffer);
    Serial.printf("[MQTT] Query sent: %s (ok=%d)\n", barcode, (int)ok);
    return ok;
}

void mqtt_create_item(const char* barcode, const char* name, const char* spec,
                      int quantity, const char* location, const char* supplier) {
    if (!mqtt.connected()) {
        Serial.println("[MQTT] Not connected, cannot create");
        return;
    }

    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    doc["barcode"] = barcode;
    doc["name"] = name;
    if (spec) doc["spec"] = spec;
    doc["quantity"] = quantity;
    if (location) doc["location"] = location;
    if (supplier) doc["supplier"] = supplier;

    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));

    mqtt.publish(MQTT_TOPIC_CREATE, buffer);
    Serial.printf("[MQTT] Create sent: %s -> %s\n", barcode, name);
}

void mqtt_set_response_callback(mqtt_response_cb_t cb) {
    response_callback = cb;
}

// ─── Stock in/out (P2) ────────────────────────────────────────────────────

static void publish_stock(const char* topic, const char* barcode, int quantity) {
    if (!mqtt.connected()) {
        Serial.println("[MQTT] Not connected, cannot publish stock");
        return;
    }
    if (quantity < 1) quantity = 1;

    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    doc["barcode"]   = barcode;
    doc["quantity"]  = quantity;

    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));
    mqtt.publish(topic, buffer);
    Serial.printf("[MQTT] %s: %s x%d\n", topic, barcode, quantity);
}

void mqtt_stock_in(const char* barcode, int quantity) {
    publish_stock(MQTT_TOPIC_STOCK_IN, barcode, quantity);
}

void mqtt_stock_out(const char* barcode, int quantity) {
    publish_stock(MQTT_TOPIC_STOCK_OUT, barcode, quantity);
}

// ─── Scan-input (INPUT mode, MQTT sub-channel) ──────────────────────────────

bool mqtt_scan_input(const char* barcode, const char* type) {
    if (!mqtt.connected()) {
        Serial.println("[MQTT] Not connected, cannot send scan-input");
        return false;
    }
    if (!barcode || !barcode[0]) return false;

    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    doc["barcode"]   = barcode;
    if (type && type[0]) doc["type"] = type;
    doc["ts"] = (uint32_t)(millis() / 1000);

    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));
    bool ok = mqtt.publish(MQTT_TOPIC_SCAN_INPUT, buffer);
    Serial.printf("[MQTT] scan-input: %s (ok=%d)\n", barcode, ok);
    return ok;
}

// ─── Stocktake session state machine (P3) ──────────────────────────────────

void mqtt_stocktake_start(void) {
    if (!mqtt.connected()) {
        Serial.println("[MQTT] Not connected, cannot start stocktake");
        return;
    }
    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;

    char buffer[128];
    serializeJson(doc, buffer, sizeof(buffer));
    mqtt.publish(MQTT_TOPIC_STOCKTAKE_START, buffer);
    Serial.println("[MQTT] stocktake/start sent");
}

void mqtt_stocktake_scan(int session_id, const char* barcode, int actual_qty) {
    if (!mqtt.connected()) {
        Serial.println("[MQTT] Not connected, cannot send stocktake scan");
        return;
    }
    JsonDocument doc;
    doc["device_id"]  = DEVICE_ID;
    doc["session_id"] = session_id;
    doc["barcode"]    = barcode;
    doc["actual_qty"] = actual_qty;

    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));
    mqtt.publish(MQTT_TOPIC_STOCKTAKE_SCAN, buffer);
    Serial.printf("[MQTT] stocktake/scan: session=%d %s qty=%d\n",
                  session_id, barcode, actual_qty);
}

void mqtt_stocktake_end(int session_id) {
    if (!mqtt.connected()) {
        Serial.println("[MQTT] Not connected, cannot end stocktake");
        return;
    }
    JsonDocument doc;
    doc["device_id"]  = DEVICE_ID;
    doc["session_id"] = session_id;

    char buffer[128];
    serializeJson(doc, buffer, sizeof(buffer));
    mqtt.publish(MQTT_TOPIC_STOCKTAKE_END, buffer);
    Serial.printf("[MQTT] stocktake/end: session=%d\n", session_id);
}

void mqtt_reconnect(void) {
    if (!wifi_is_connected()) return;

    Serial.printf("[MQTT] Connecting to %s:%u...\n", s_broker, s_port);

    String client_id = String("esp32-scanner-") + String(random(0xffff), HEX);

    if (mqtt.connect(client_id.c_str(), MQTT_USER, MQTT_PASSWORD)) {
        Serial.println("[MQTT] Connected!");

        // Subscribe to response topic
        mqtt.subscribe(MQTT_TOPIC_RESPONSE);
        Serial.printf("[MQTT] Subscribed to: %s\n", MQTT_TOPIC_RESPONSE);
    } else {
        Serial.printf("[MQTT] Failed, rc=%d\n", mqtt.state());
    }
}

uint16_t mqtt_publish_inventory_batch(void) {
    // DEPRECATED (Redmine #79): this used to wrap pending scan-log lines into a
    // {device_id,count,items} envelope and publish them to warehouse/create.
    // That payload violates the server's _handle_create contract (which expects
    // a single {barcode,name,...} item) and was rejected as "Invalid payload".
    // Stocktake now uses the asynchronous session state machine
    // (mqtt_stocktake_start/scan/end). This batch path is retained only so the
    // offline sync caller keeps compiling; it performs no publish. A correct
    // offline-replay implementation is tracked separately.
    uint32_t pending = scan_log_pending_count();
    if (pending > 0) {
        Serial.printf("[MQTT][INV] batch publish disabled (deprecated); "
                      "%u pending item(s) left untouched\n", pending);
    }
    return 0;
}
