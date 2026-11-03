/**
 * @file ui_notify.h
 * @brief Minimal LVGL notification screen for the ANCS LCD test.
 *
 * Shows the most recent iPhone notification: category, title, message.
 * ui_notify_post() is cross-task safe (call from the BLE callback); the actual
 * LVGL widget update happens in ui_notify_drain() on the LVGL task.
 */
#ifndef UI_NOTIFY_H
#define UI_NOTIFY_H

/** Build the LVGL notification screen. Call once after display_init(). */
void ui_notify_init(void);

/** Set a status line (e.g. "Advertising", "Connected", "Subscribed"). Cross-task safe. */
void ui_notify_set_status(const char* status);

/** Post a notification to display. Cross-task safe (BLE callback context). */
void ui_notify_post(const char* category, const char* when, const char* title, const char* message);

/** Apply any pending status/notification to LVGL widgets. Call from LVGL task. */
void ui_notify_drain(void);

#endif /* UI_NOTIFY_H */
