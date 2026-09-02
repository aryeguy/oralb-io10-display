#pragma once

#include "brush_snapshot.hpp"
#include <cstddef>
#include <cstdint>

class OralBAdvParser {
public:
    // NimBLE manufacturer-data normally includes the two-byte company ID.
    // This accepts either:
    //   DC 00 + 9/11 byte Oral-B payload
    // or a bare 9/11 byte Oral-B payload.
    bool parse(
        const uint8_t* data,
        size_t length,
        uint32_t receivedAtMs,
        BrushSnapshot& out
    ) const;

    static constexpr uint16_t kManufacturerId = 0x00DC;

private:
    Pressure decodePressure(uint8_t raw) const;
    uint8_t decodeSector(uint8_t raw, uint8_t sectorCount) const;
};
