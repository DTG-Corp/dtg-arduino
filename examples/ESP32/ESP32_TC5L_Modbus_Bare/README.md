# ESP32 TC5-L Modbus Bare Bench

Standalone diagnostic sketch - **no WiFi, MQTT, or dtg library**.

## Flash

1. Arduino IDE: open `ESP32_TC5L_Modbus_Bare.ino`
2. Board: ESP32 (your devkit)
3. Serial Monitor: **115200**
4. Upload

## Expected output (success)

```
[poll] PV FC03 addr=0
TX: 01 03 00 00 00 01 84 0A
RX: 01 03 02 00 FF F8 39
CRC: OK  raw=255  scaled=25.5
```

## Test matrix

Edit `#define` at top of `.ino`, re-flash after each change:

| Define | Try when |
|--------|----------|
| `MODBUS_PARITY_MODE 0` | 8N1 (default, matches USB Modbus None) |
| `MODBUS_PARITY_MODE 1` | 8E1 (many TC5-L panels) |
| Swap `MODBUS_RX_PIN` / `MODBUS_TX_PIN` | TTL TX/RX reversed on ESP32 |
| `MODBUS_BAUD 4800` | Panel `bAud` not 9600 |

## Interpret results

| RX line | Meaning |
|---------|---------|
| `(0 bytes)` | Physical bus / module - not IoT Platform |
| bytes, CRC FAIL | baud/parity/noise |
| CRC OK | ESP32 path works - compare with full gateway sketch |

## Next step

If bare works but `ESP32_WiFi_Modbus` still times out, the dtg stack fix in `modbus_bus.cpp` (auto-direction RX drain + turnaround) applies to the full gateway automatically.
