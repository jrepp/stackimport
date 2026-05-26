#pragma once

#include <cstddef>
#include <cstdint>

namespace stackimport {

bool DecodeIconBwWithRsrcd(const uint8_t* data, size_t size, uint8_t* rgba, size_t rgbaSize);
bool DecodeCursorWithRsrcd(const uint8_t* data, size_t size, uint8_t* rgba, size_t rgbaSize, int16_t& hotX, int16_t& hotY);
size_t PatternListCountWithRsrcd(const uint8_t* data, size_t size);
bool DecodePatternAtWithRsrcd(const uint8_t* data, size_t size, size_t index, uint8_t* rgba, size_t rgbaSize);

struct PlteButton {
	int16_t top = 0;
	int16_t left = 0;
	int16_t bottom = 0;
	int16_t right = 0;
	const uint8_t* message = nullptr;
	size_t messageSize = 0;
};

struct PltePalette {
	uint16_t wdef = 0;
	bool showName = false;
	int16_t selection = 0;
	int16_t frame = 0;
	int32_t pictRef = 0;
	int16_t top = 0;
	int16_t left = 0;
	size_t buttonCount = 0;
	PlteButton buttons[64];
};

bool ParsePlteWithRsrcd(const uint8_t* data, size_t size, PltePalette& palette);

} // namespace stackimport
