#include "oralb_adv_parser.hpp"

#include <cstring>

Pressure OralBAdvParser::decodePressure(uint8_t raw) const {
    // oralb-ble treats manufacturer byte 4 as a bit field.
    //
    // bit 7 = high pressure
    // bit 3 = power button
    // bit 2 = mode button
    //
    // Passive ads distinguish normal/high. "Low" is available from the
    // active pressure characteristic, not this advertisement field.
    if (raw & 0x80) {
        return Pressure::High;
    }
    return Pressure::Normal;
}

uint8_t OralBAdvParser::decodeSector(uint8_t raw, uint8_t sectorCount) const {
    uint8_t q = raw & 0x07;

    if (q == 0) {
        return 0;
    }

    // 7 is the "last sector" sentinel. For iO 10 with 6 pacer sectors,
    // use byte 10's low 3 bits.
    if (q == 7) {
        const uint8_t count = sectorCount & 0x07;
        return count ? count : 4;
    }

    return q;
}

bool OralBAdvParser::parse(
    const uint8_t* input,
    size_t inputLength,
    uint32_t receivedAtMs,
    BrushSnapshot& out
) const {
    if (input == nullptr) {
        return false;
    }

    const uint8_t* data = input;
    size_t length = inputLength;

    // BLE manufacturer specific data starts with company ID in little-endian.
    if (length >= 2 && data[0] == 0xDC && data[1] == 0x00) {
        data += 2;
        length -= 2;
    }

    if (length != 9 && length != 11) {
        return false;
    }

    // byte 1 is model type. iO family known values include 48/49/50/52/53/54.
    const uint8_t model = data[1];
    const bool ioFamily =
        model == 48 || model == 49 || model == 50 ||
        model == 52 || model == 53 || model == 54;

    if (!ioFamily) {
        return false;
    }

    const uint8_t state = data[3];
    const uint8_t pressureRaw = data[4];
    const uint32_t brushingTime = static_cast<uint32_t>(data[5]) * 60U + data[6];
    const uint8_t mode = data[7];
    const uint8_t sectorRaw = data[8];

    uint8_t sectorTimer = 0;
    uint8_t sectorCount = 0;

    if (length == 11) {
        sectorTimer = data[9];
        sectorCount = data[10];
    }

    out = BrushSnapshot{};
    out.valid = true;
    out.brushing = (state == 3);
    out.elapsedSeconds = brushingTime;
    out.pressure = decodePressure(pressureRaw);
    out.modeRaw = mode;
    out.pacerSectorCount = sectorCount;
    out.pacerSectorTimer = sectorTimer;
    out.pacerSector = out.brushing
        ? decodeSector(sectorRaw, sectorCount)
        : 0;
    out.receivedAtMs = receivedAtMs;

    return true;
}
