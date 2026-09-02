// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bandwidth.h"

static uint32_t clock_samples[4];
static uint32_t clock_sample_count;
static uint32_t clock_sample_index;

uint32_t time_get_ms(void)
{
    assert(clock_sample_index < clock_sample_count);
    return clock_samples[clock_sample_index++];
}

static void set_clock(uint32_t first, uint32_t second, uint32_t count)
{
    clock_samples[0] = first;
    clock_samples[1] = second;
    clock_sample_count = count;
    clock_sample_index = 0;
}

static void test_layout_and_signatures(void)
{
    _Static_assert(offsetof(bandwidth_t, queue_size) == 0x00, "queue_size moved");
    _Static_assert(offsetof(bandwidth_t, queue_time) == 0x04, "queue_time moved");
    _Static_assert(offsetof(bandwidth_t, bandwidth) == 0x08, "bandwidth moved");
    _Static_assert(offsetof(bandwidth_t, autoadjust) == 0x0C, "autoadjust moved");
    _Static_assert(sizeof(bandwidth_t) == 0x10, "bandwidth_t size changed");
    _Static_assert(_Generic(&bandwidth_init, void (*)(bandwidth_t *): 1, default: 0), "bandwidth_init signature changed");
    _Static_assert(_Generic(&bandwidth_enqueue_bytes, void (*)(bandwidth_t *, uint32_t): 1, default: 0),
                   "bandwidth_enqueue_bytes signature changed");
    _Static_assert(_Generic(&bandwidth_get_queue_size, uint32_t (*)(bandwidth_t *): 1, default: 0),
                   "bandwidth_get_queue_size signature changed");
    _Static_assert(_Generic(&bandwidth_get_time_empty, uint32_t (*)(bandwidth_t *): 1, default: 0),
                   "bandwidth_get_time_empty signature changed");
}

static void test_init_selectivity(void)
{
    bandwidth_t bandwidth;

    memset(&bandwidth, 0xA5, sizeof(bandwidth));
    set_clock(1234, 0, 1);
    bandwidth_init(&bandwidth);
    assert(clock_sample_index == 1);
    assert(bandwidth.queue_size == 0);
    assert(bandwidth.queue_time == 1234);
    assert(bandwidth.bandwidth == 3000);
    assert(bandwidth.autoadjust == UINT32_C(0xA5A5A5A5));
}

static void test_enqueue_clock_order(void)
{
    bandwidth_t bandwidth = {100, 100, 1000, 0x11223344};

    set_clock(150, 160, 2);
    bandwidth_enqueue_bytes(&bandwidth, 25);
    assert(clock_sample_index == 2);
    assert(bandwidth.queue_size == 75);
    assert(bandwidth.queue_time == 160);
    assert(bandwidth.bandwidth == 1000);
    assert(bandwidth.autoadjust == UINT32_C(0x11223344));

    bandwidth.queue_size = 0;
    set_clock(200, 0, 1);
    bandwidth_enqueue_bytes(&bandwidth, 7);
    assert(clock_sample_index == 1);
    assert(bandwidth.queue_size == 7);
    assert(bandwidth.queue_time == 200);
}

static void test_queue_drain(void)
{
    bandwidth_t bandwidth = {100, UINT32_MAX - 9u, 1000, 0};

    set_clock(10, 0, 1);
    assert(bandwidth_get_queue_size(&bandwidth) == 80);
    assert(clock_sample_index == 1);
    assert(bandwidth.queue_time == 10);

    bandwidth.queue_size = 20;
    bandwidth.queue_time = 100;
    bandwidth.bandwidth = 1000;
    set_clock(121, 0, 1);
    assert(bandwidth_get_queue_size(&bandwidth) == 0);
    assert(bandwidth.queue_time == 121);

    bandwidth.queue_size = 0;
    set_clock(999, 0, 1);
    assert(bandwidth_get_queue_size(&bandwidth) == 0);
    assert(clock_sample_index == 0);
}

static void test_recovered_multiply_width(void)
{
    bandwidth_t bandwidth = {6000000, 0, 5000000, 0};

    set_clock(1000, 0, 1);
#if defined(RDPLIB_SOURCE_FAITHFUL) || defined(RDPLIB_DEBUG)
    assert(bandwidth_get_queue_size(&bandwidth) == 5294968);
#else
    assert(bandwidth_get_queue_size(&bandwidth) == 1000000);
#endif
}

static void test_time_empty(void)
{
    bandwidth_t bandwidth = {250, 1000, 1000, 0};

    assert(bandwidth_get_time_empty(&bandwidth) == 1250);
    assert(bandwidth_get_send_speed(&bandwidth) == 1000);
}

#ifdef RDP_DEAD_CODE
static void test_dead_automatic_rate_helpers(void)
{
    bandwidth_t bandwidth = {0, 0, 3000, 7};

    set_clock(77, 0, 1);
    bandwidth_set_queue_size(&bandwidth, 12);
    assert(bandwidth.queue_size == 12);
    assert(bandwidth.queue_time == 77);

    assert(bandwidth_stepup(&bandwidth) == 3030);
    bandwidth.bandwidth = 6000;
    assert(bandwidth_stepup(&bandwidth) == 6000);
    bandwidth.bandwidth = 3000;
    assert(bandwidth_stepdown(&bandwidth) == 2940);
    bandwidth.bandwidth = 1000;
    assert(bandwidth_stepdown(&bandwidth) == 1000);

    bandwidth_set_send_speed(&bandwidth, 4321);
    assert(bandwidth.bandwidth == 4321);
    assert(bandwidth.autoadjust == 0);
    bandwidth_set_send_speed(&bandwidth, 0);
    assert(bandwidth.bandwidth == 4321);
    assert(bandwidth.autoadjust == 1);
}
#endif

int main(void)
{
    test_layout_and_signatures();
    test_init_selectivity();
    test_enqueue_clock_order();
    test_queue_drain();
    test_recovered_multiply_width();
    test_time_empty();
#ifdef RDP_DEAD_CODE
    test_dead_automatic_rate_helpers();
#endif
    return 0;
}
