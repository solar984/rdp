// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include <stddef.h>
#include <stdint.h>

#include "test_assert.h"
#include "iov.h"

_Static_assert(offsetof(iov_t, data) == 0, "iov_t::data moved");
_Static_assert(offsetof(iov_t, size) == sizeof(void *), "iov_t::size moved");
_Static_assert(sizeof(iov_t) == (sizeof(void *) == 4 ? 8 : 16), "iov_t native layout changed");
_Static_assert(_Generic(&((iov_t *)0)->data, void **: 1, default: 0), "iov_t::data type changed");
_Static_assert(_Generic(&((iov_t *)0)->size, uint32_t *: 1, default: 0), "iov_t::size type changed");

int main(void)
{
    uint8_t first = 0x11;
    uint8_t second = 0x22;
    iov_t vectors[2] = {{&first, 1}, {&second, 0xffffffffu}};

    assert(vectors[0].data == &first);
    assert(vectors[0].size == 1);
    assert(vectors[1].data == &second);
    assert(vectors[1].size == UINT32_MAX);
    assert((size_t)((uint8_t *)&vectors[1] - (uint8_t *)&vectors[0]) == sizeof(iov_t));
    return 0;
}
