/**
 * Host self-check for Modbus transformers (Arduino-free).
 * g++ -std=c++17 -fsanitize=address,undefined -I../src -o modbus_codec_test modbus_codec_test.cpp ../src/modbus/modbus_codec.cpp
 */
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/modbus/modbus_codec.h"

static void testInt16()
{
  const uint16_t regs[] = {0xFF9C}; // -100
  float out = 0.0f;
  assert(detail::decodeModbusRegisters(regs, 1, "INT16", 0.1f, 0.0f, out));
  assert(out < -9.9f && out > -10.1f);
}

static void testUint16Offset()
{
  const uint16_t regs[] = {255};
  float out = 0.0f;
  assert(detail::decodeModbusRegisters(regs, 1, "UINT16", 0.1f, -1.0f, out));
  assert(out > 24.4f && out < 24.6f);
}

static void testUint32BeLe()
{
  const uint16_t regs[] = {0x0001, 0x0002};
  float be = 0.0f;
  float le = 0.0f;
  assert(detail::decodeModbusRegisters(regs, 2, "UINT32_BE", 1.0f, 0.0f, be));
  assert(detail::decodeModbusRegisters(regs, 2, "UINT32_LE", 1.0f, 0.0f, le));
  assert(be == 65538.0f); // 0x00010002
  assert(le == 131073.0f); // 0x00020001
}

static void testMidSwap()
{
  const uint16_t regs[] = {0xAABB, 0xCCDD};
  float mid = 0.0f;
  float be = 0.0f;
  assert(detail::decodeModbusRegisters(regs, 2, "UINT32_MID_BE", 1.0f, 0.0f, mid));
  assert(detail::decodeModbusRegisters(regs, 2, "UINT32_BE", 1.0f, 0.0f, be));
  assert(mid != be);
}

static void testBits()
{
  const uint8_t bytes[] = {0x05};
  float out = 0.0f;
  assert(detail::decodeModbusBits(bytes, 1, 1, 1.0f, 0.0f, out));
  assert(out == 1.0f);
  assert(detail::decodeModbusBits(bytes, 1, 3, 1.0f, 0.0f, out));
  assert(out == 5.0f);
}

static void testEncode()
{
  uint16_t raw = 0;
  assert(detail::encodeModbusRegisterValue(10.0f, "INT16", 0.1f, 0.0f, raw));
  assert(raw == 100);
}

int main()
{
  testInt16();
  testUint16Offset();
  testUint32BeLe();
  testMidSwap();
  testBits();
  testEncode();
  std::puts("modbus_codec_test OK");
  return 0;
}
