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
 */
void mqtt_query_barcode(const char* barcode);

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

/**
 * @brief Reconnect MQTT if disconnected.
 */
void mqtt_reconnect(void);

#endif /* MQTT_CLIENT_H */
