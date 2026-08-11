// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "container.h"

int uint8_cmp(const void *left, const void *right)
{
    const uint8_t difference = (uint8_t)(*(const uint8_t *)left - *(const uint8_t *)right);
    const int8_t signed_difference = (int8_t)difference;

    if (signed_difference > 0)
    {
        return 1;
    }
    if (signed_difference < 0)
    {
        return -1;
    }
    return 0;
}

int uint16_cmp(const void *left, const void *right)
{
    const uint16_t difference = (uint16_t)(*(const uint16_t *)left - *(const uint16_t *)right);
    const int16_t signed_difference = (int16_t)difference;

    if (signed_difference > 0)
    {
        return 1;
    }
    if (signed_difference < 0)
    {
        return -1;
    }
    return 0;
}
