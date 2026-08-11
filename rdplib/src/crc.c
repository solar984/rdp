// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "framing.h"

uint32_t rdp_crc(uint32_t seed, const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = ~seed;
    uint32_t index;

    for (index = 0; index < length; ++index)
    {
        uint32_t bit;

        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit)
        {
            uint32_t low_bit = crc & 1u;
            crc >>= 1;
            if (low_bit)
            {
                crc ^= 0xEDB88320u;
            }
        }
    }

    return ~crc;
}
