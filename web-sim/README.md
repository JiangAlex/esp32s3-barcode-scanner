# Web LVGL Simulator

Browser-based simulator for the ESP32 Barcode Scanner LVGL UI.

## Quick Start

```bash
cd web-sim/
python3 -m http.server 8765
# Open http://localhost:8765 in your browser
```

## Controls

### On-Screen Buttons
- **▲ UP / ▼ DOWN / ◀ LEFT / RIGHT ▶** — Navigate
- **A / BOOT** — Confirm (short press = navigate, long press >1s = confirm, hold >2s = home)
- **Simulate Scan** — Trigger a fake barcode scan
- **Toggle WiFi / Toggle MQTT** — Simulate connection status

### Keyboard Shortcuts
| Key | Action |
|-----|--------|
| `Enter` or `Space` | Boot button (long press = home) |
| `Arrow keys` | Navigate |
| `S` | Simulate scan |
| `M` | Cycle scan mode |
| `H` | Return home |
| `1` | Toggle WiFi |
| `2` | Toggle MQTT |
| `Escape` | Return home |

## Pages

- **HOME** — Main menu: Scan, Settings, About
- **SCAN** — Barcode scanning with 5 modes: QUERY, INPUT, IN, OUT, COUNT
- **SETTINGS** — Brightness slider
- **INVENTORY** — Stocktake session management
- **PHOTO** — Photo capture simulation
