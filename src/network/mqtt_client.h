/**
 * @file mqtt_client.h
 * @brief MQTT client for warehouse communication
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Callback type for MQTT response messages
typedef void (*mqtt_response_cb_t)(const char* status, JsonDocument& data);

/**
 * @brief Initialize MQTT client and connect to broker.
 */
void mqtt_client_init(void);

/**
 * @brief Override the MQTT broker host/port at runtime (e.g. from the WiFi
 *        config portal). Call before mqtt_client_init()/mqtt_reconnect().
 *        Empty host or 0 port keeps the current/default value.
 */
void mqtt_set_broker(const char* host, uint16_t port);

/**
 * @brief Must be called in loop() for MQTT keep-alive.
 */
void mqtt_client_loop(void);

/**
 * @brief Check if MQTT is connected.
 */
bool mqtt_is_connected(void);

/**
 * @brief Send a barcode query to the warehouse server.
 * @param barcode Barcode string to look up
 * @return true if the query was published (MQTT connected), false otherwise.
 */
bool mqtt_query_barcode(const char* barcode);

/**
 * @brief Send a create-item request to the warehouse server.
 * @param barcode Barcode string
 * @param name Item name
 * @param spec Item specification
 * @param quantity Item quantity
 * @param location Storage location
 * @param supplier Supplier name
 */
void mqtt_create_item(const char* barcode, const char* name, const char* spec,
                      int quantity, const char* location, const char* supplier);

/**
 * @brief Register callback for MQTT response messages.
 */
void mqtt_set_response_callback(mqtt_response_cb_t cb);

// ─── Stock in/out (P2) ────────────────────────────────────────────────────
/**
 * @brief Publish a stock-in scan to the server.
 *        Server contract: warehouse/stock/in {device_id, barcode, quantity}.
 *        The server increments item quantity and logs an InventoryLog.
 * @param barcode  Scanned barcode
 * @param quantity Units to add (defaults handled by caller; >=1)
 */
void mqtt_stock_in(const char* barcode, int quantity);

/**
 * @brief Publish a stock-out scan to the server.
 *        Server contract: warehouse/stock/out {device_id, barcode, quantity}.
 * @param barcode  Scanned barcode
 * @param quantity Units to remove (>=1)
 */
void mqtt_stock_out(const char* barcode, int quantity);

// ─── Scan-input (INPUT mode, MQTT sub-channel) ──────────────────────────────
/**
 * @brief Publish a scanned barcode to the server for live relay to the Web UI.
 *        Server contract: warehouse/scan-input/{device_id}
 *        {device_id, barcode, type?, ts?}. The server pushes it to any Web UI
 *        browser via SSE (no DB write, no response). Used by the INPUT mode's
 *        MQTT sub-channel (vs the BLE HID sub-channel).
 * @param barcode Scanned barcode/content
 * @param type    Optional type tag (e.g. "QR", "CODE128"); may be nullptr
 * @return true if published (MQTT connected), false otherwise.
 */
bool mqtt_scan_input(const char* barcode, const char* type);

// ─── Stocktake session state machine (P3) ──────────────────────────────────
/**
 * @brief Begin a stocktake session on the server.
 *        Publishes warehouse/stocktake/start {device_id}. The server replies
 *        asynchronously with status "stocktake_started" carrying session_id;
 *        the response handler must feed it back via the UI state machine.
 */
void mqtt_stocktake_start(void);

/**
 * @brief Submit one counted barcode within an open stocktake session.
 *        Publishes warehouse/stocktake/scan
 *        {device_id, session_id, barcode, actual_qty}.
 * @param session_id Session id returned by the server at start
 * @param barcode    Counted barcode
 * @param actual_qty Physically counted quantity for this barcode
 */
void mqtt_stocktake_scan(int session_id, const char* barcode, int actual_qty);

/**
 * @brief Close an open stocktake session.
 *        Publishes warehouse/stocktake/end {device_id, session_id}.
 * @param session_id Session id to close
 */
void mqtt_stocktake_end(int session_id);

/**
 * @brief Reconnect MQTT if disconnected.
 */
void mqtt_reconnect(void);

/**
 * @brief Publish a batch of inventory items to the server.
 *        Iterates pending scan log entries and publishes each.
 * @return number of items published
 */
uint16_t mqtt_publish_inventory_batch(void);

#endif /* MQTT_CLIENT_H */
