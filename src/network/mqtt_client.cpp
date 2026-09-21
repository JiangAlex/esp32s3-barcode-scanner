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

void mqtt_client_init(void) {
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(mqtt_message_cb);
    mqtt.setBufferSize(1024);  // Increase buffer for JSON payloads

    if (wifi_is_connected()) {
        mqtt_reconnect();
    }
}

void mqtt_client_loop(void) {
    if (!mqtt.connected()) {
        static unsigned long last_attempt = 0;
        if (millis() - last_attempt > 5000) {  // Retry every 5 seconds
            last_attempt = millis();
            mqtt_reconnect();
        }
    }
    mqtt.loop();
}

bool mqtt_is_connected(void) {
    return mqtt.connected();
}

void mqtt_query_barcode(const char* barcode) {
    if (!mqtt.connected()) {
        Serial.println("[MQTT] Not connected, cannot query");
        return;
    }

    JsonDocument doc;
    doc["barcode"] = barcode;
    doc["device_id"] = DEVICE_ID;

    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));

    mqtt.publish(MQTT_TOPIC_QUERY, buffer);
    Serial.printf("[MQTT] Query sent: %s\n", barcode);
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

void mqtt_reconnect(void) {
    if (!wifi_is_connected()) return;

    Serial.printf("[MQTT] Connecting to %s:%d...\n", MQTT_BROKER, MQTT_PORT);

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
    if (!mqtt.connected()) {
        Serial.println("[MQTT] Not connected, cannot publish batch");
        return 0;
    }

    uint32_t pending = scan_log_pending_count();
    if (pending == 0) {
        Serial.println("[MQTT][INV] no pending items to upload");
        return 0;
    }

    // Read pending entries as JSON array
    String payload = scan_log_read_pending();
    Serial.printf("[MQTT][INV] publishing %u items...\n", pending);

    // Wrap in a batch envelope
    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    doc["count"] = pending;
    doc["items"] = payload;

    char buffer[2048];
    serializeJson(doc, buffer, sizeof(buffer));

    mqtt.publish(MQTT_TOPIC_CREATE, buffer);
    Serial.printf("[MQTT][INV] Batch published (%u items)\n", pending);

    // Clear pending after successful publish (best-effort)
    // The server will ack; if we lose it, items remain in pending for next sync
    return pending;
}
