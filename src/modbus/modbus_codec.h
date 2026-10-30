#pragma once

#include <stddef.h>
#include <stdint.h>

namespace detail {

bool decodeModbusRegisters(const uint16_t *regs, size_t count, const char *transformer,
                           float scale, float offset, float &valueOut);

bool encodeModbusRegisterValue(float value, const char *transformer, float scale, float offset,
                               uint16_t &rawOut);

// Decode packed coil/discrete-input bits (FC01/FC02) into a numeric value.
// For length==1 this yields 0/1; for length>1 the first `bitCount` bits form an
// unsigned integer (LSB first), then scale/offset are applied.
bool decodeModbusBits(const uint8_t *bytes, size_t byteCount, uint16_t bitCount, float scale,
                      float offset, float &valueOut);

} // namespace detail
