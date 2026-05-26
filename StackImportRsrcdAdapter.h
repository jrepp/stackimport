#pragma once

#include <cstddef>
#include <cstdint>

namespace stackimport {

bool DecodeIconBwWithRsrcd(const uint8_t* data, size_t size, uint8_t* rgba, size_t rgbaSize);
bool DecodeCursorWithRsrcd(const uint8_t* data, size_t size, uint8_t* rgba, size_t rgbaSize, int16_t& hotX, int16_t& hotY);
size_t PatternListCountWithRsrcd(const uint8_t* data, size_t size);
bool DecodePatternAtWithRsrcd(const uint8_t* data, size_t size, size_t index, uint8_t* rgba, size_t rgbaSize);

} // namespace stackimport
