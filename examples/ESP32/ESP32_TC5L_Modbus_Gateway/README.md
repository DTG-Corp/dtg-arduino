# ESP32 TC5-L Modbus Gateway

Full IoT Platform gateway for Sinny **TC5-L** via Modbus RTU on UART2.

## Prerequisites

1. **Bare bench OK** - flash `../ESP32_TC5L_Modbus_Bare` first; Serial Monitor must show CRC OK for addr 0 (PV) and 1 (SV).
2. **Hub channel** - MODBUS_RTU, `UART2`, 9600, parity `none` (8N1), auto-direction (`dePin`/`rePin` omitted).
3. **Device template** - apply system template **TC5-L** (`tpl_tc5l_v1`) or create tags manually:
   - `pv`: FC03 addr 0, INT16
   - `sv`: FC03 addr 1, INT16

## Flash

1. Open `ESP32_TC5L_Modbus_Gateway.ino`
2. Set Wi-Fi + gateway MQTT credentials
3. Arduino IDE **Tools → Partition Scheme** → **Minimal SPIFFS (1.9MB APP with OTA)** if upload fails with *text section exceeds available space* (default 1.2MB APP is tight for Wi-Fi + TLS + Modbus).
4. Upload; Serial @ 115200

This sketch sets `DTG_WIFI_GATEWAY_ONLY` before `#include <dtg.h>` to omit unused cellular/GPS code from the binary.

## Verify

| Check | Topic / signal |
|-------|----------------|
| Config applied | `up/modbus/cfg_ack` status `applied` |
| PV telemetry | `tele/modbus/ch0/tc5l/pv` (aliases may differ) |
| Bus health | `up/modbus/ch0/health` - `ok` increases, `autoDir: true` |
| Debug trace | Set `DTG_MODBUS_TRACE` to `1` in `dtg_config.h` (or `-D DTG_MODBUS_TRACE=1` build flag), then reflash |

## Pin override

Library defaults follow common ESP32 UART2 pins. This example remaps UART GPIO for its bench wiring - override further in sketch or Hub channel `rxPin`/`txPin`.
