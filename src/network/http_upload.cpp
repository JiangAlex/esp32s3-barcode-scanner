/**
 * @file http_upload.cpp
 * @brief HTTP POST photo upload implementation using WiFiClient
 */

#include "http_upload.h"
#include <WiFi.h>
#include <Arduino.h>
#include "config/config.h"

// Default recognition server (same host as MQTT broker for now)
#ifndef SERVER_URL
#define SERVER_URL   "192.168.1.100"
#endif

#ifndef SERVER_PORT
#define SERVER_PORT  8080
#endif

#define HTTP_TIMEOUT_MS  10000

bool http_upload_photo(const uint8_t* jpg_buf, size_t jpg_len,
                       char* result_buf, size_t result_buf_size) {
    if (!jpg_buf || jpg_len == 0 || !result_buf || result_buf_size == 0) {
        Serial.println("[HTTP] Invalid parameters");
        return false;
    }

    WiFiClient client;
    if (!client.connect(SERVER_URL, SERVER_PORT)) {
        Serial.println("[HTTP] Connection failed");
        return false;
    }

    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    // Build multipart/form-data request
    String header = String();
    header += "POST /api/photo-recognize HTTP/1.1\r\n";
    header += "Host: " SERVER_URL "\r\n";
    header += "User-Agent: ESP32S3-Scanner/1.0\r\n";
    header += "Content-Type: multipart/form-data; boundary=----ESP32Form\r\n";
    header += "Connection: close\r\n";

    // Multipart body
    String body = String();
    body += "------ESP32Form\r\n";
    body += "Content-Disposition: form-data; name=\"image\"; filename=\"photo.jpg\"\r\n";
    body += "Content-Type: image/jpeg\r\n";
    body += "\r\n";

    // Calculate content length
    size_t body_len = body.length() + jpg_len + strlen("\r\n------ESP32Form--\r\n");
    header += "Content-Length: " + String(body_len) + "\r\n";
    header += "\r\n";

    // Send header
    client.print(header);

    // Send multipart header
    client.print(body);

    // Send JPEG data in chunks to avoid OOM on large frames
    const size_t chunk_size = 4096;
    size_t sent = 0;
    while (sent < jpg_len) {
        size_t chunk = (jpg_len - sent > chunk_size) ? chunk_size : (jpg_len - sent);
        if (client.write(jpg_buf + sent, chunk) != chunk) {
            Serial.println("[HTTP] Write error");
            client.stop();
            return false;
        }
        sent += chunk;
    }

    // Send multipart footer
    client.print("\r\n------ESP32Form--\r\n");

    // Read response
    result_buf[0] = '\0';
    bool found_body = false;
    unsigned long start = millis();

    while (client.connected() && millis() - start < HTTP_TIMEOUT_MS) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();

            if (line.isEmpty() && !found_body) {
                found_body = true;
                continue;
            }

            if (found_body) {
                size_t needed = line.length() + 1;
                if (needed <= result_buf_size) {
                    line.toCharArray(result_buf, result_buf_size);
                } else {
                    // Truncate if buffer too small
                    line.substring(0, result_buf_size - 1).toCharArray(result_buf, result_buf_size);
                }
                break;
            }
        }
        delay(1);
    }

    client.stop();

    if (!found_body) {
        Serial.println("[HTTP] No response body received");
        return false;
    }

    Serial.printf("[HTTP] Response received (%d bytes)\n", (int)strlen(result_buf));
    return true;
}
