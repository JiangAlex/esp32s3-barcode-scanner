/**
 * @file lv_conf.h
 * @brief LVGL v8.4 Configuration for ESP32-S3 Barcode Scanner
 *        240x320 TFT LCD, 16-bit color (RGB565)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Color settings */
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        1       // Byte swap for SPI TFT (big-endian)

/* Memory settings — use PSRAM for draw buffer */
#define LV_MEM_CUSTOM           0
#define LV_MEM_SIZE             (96U * 1024U)   // 96KB for LVGL internal heap

/* Display refresh */
#define LV_DISP_DEF_REFR_PERIOD 16      // ~60fps target
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_DPI_DEF              130

/* Tick source — use Arduino millis() */
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Drawing features */
#define LV_USE_GPU_ESP32_DMA2D  0
#define LV_DRAW_COMPLEX         1
#define LV_SHADOW_CACHE_SIZE    0
#define LV_IMG_CACHE_DEF_SIZE   0

/* Logging */
#define LV_USE_LOG              0
#if LV_USE_LOG
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1
#endif

/* Asserts */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_USE_ASSERT_STYLE         0

/* Font settings */
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_UNSCII_8        1
#define LV_FONT_UNSCII_16       0
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* Font rendering */
#define LV_FONT_FMT_TXT_LARGE  0
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_FONT_SUBPX          0
#define LV_USE_FONT_COMPRESSED  0

/* Text settings */
#define LV_TXT_ENC             LV_TXT_ENC_UTF8

/* Widget usage */
#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_BTN              1
#define LV_USE_BTNMATRIX        1
#define LV_USE_CANVAS           1       // Needed for QR code rendering
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_IMG              1
#define LV_USE_LABEL            1
#define LV_USE_LINE             1
#define LV_USE_ROLLER           1
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_TEXTAREA         1
#define LV_USE_TABLE            1

/* Extra widgets */
#define LV_USE_ANIMIMG          0
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0
#define LV_USE_COLORWHEEL       0
#define LV_USE_IMGBTN           1
#define LV_USE_KEYBOARD         1       // For text input (barcode create)
#define LV_USE_LED              1
#define LV_USE_LIST             1       // For scan history list
#define LV_USE_MENU             0
#define LV_USE_METER            0
#define LV_USE_MSGBOX           1
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          1
#define LV_USE_SPINNER          1       // Loading indicator
#define LV_USE_TABVIEW          1       // Main navigation tabs
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

/* Themes */
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   0

/* Layouts */
#define LV_USE_FLEX             1
#define LV_USE_GRID             1

/* File system (for SD card image loading) */
#define LV_USE_FS_STDIO         0
#define LV_USE_FS_POSIX         0
#define LV_USE_FS_FATFS         0

/* Other */
#define LV_USE_SNAPSHOT         0
#define LV_USE_MONKEY           0
#define LV_USE_GRIDNAV          0
#define LV_USE_FRAGMENT         0
#define LV_USE_IMGFONT          0
#define LV_USE_IME_PINYIN       0

/* Animations */
#define LV_USE_ANIMATION        1
#define LV_USE_SHADOW           1
#define LV_USE_BLEND_MODES      1
#define LV_USE_OPA_SCALE        1
#define LV_USE_IMG_TRANSFORM    1
#define LV_USE_GROUP            1
#define LV_USE_USER_DATA        1

/* Anti-aliasing */
#define LV_ANTIALIAS            1

#endif /* LV_CONF_H */
