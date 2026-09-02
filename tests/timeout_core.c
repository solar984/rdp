// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <string.h>

#include "timeout.h"

static void test_signatures_and_layout(void)
{
    void (*init_fn)(timeout_t *, uint32_t, uint16_t) = timeout_init;
    void (*add_sample_fn)(timeout_t *, uint32_t, uint16_t) = timeout_add_sample;
    uint16_t (*get_timeout_fn)(timeout_t *) = timeout_get_timeout;
    uint16_t (*get_ancient_fn)(timeout_t *) = timeout_get_ancient;

    (void)init_fn;
    (void)add_sample_fn;
    (void)get_timeout_fn;
    (void)get_ancient_fn;

    assert(sizeof(rt_sample_t) == 4u);
    assert(offsetof(rt_sample_t, time_rt) == 0u);
    assert(offsetof(rt_sample_t, weight) == 2u);
#if defined(_WIN32) && !defined(_WIN64)
    assert(sizeof(timeout_t) == 0x120u);
    assert(offsetof(timeout_t, oldest_sample) == 0x100u);
    assert(offsetof(timeout_t, sum_weighted_squares) == 0x108u);
    assert(offsetof(timeout_t, sum_weighted_times) == 0x110u);
    assert(offsetof(timeout_t, sum_weight) == 0x114u);
    assert(offsetof(timeout_t, weighted_avg) == 0x116u);
    assert(offsetof(timeout_t, std_deviation) == 0x118u);
#endif
}

static void test_initialization(void)
{
    timeout_t first;
    timeout_t second;

    timeout_init(&first, 500, 1);
    timeout_init(&second, 500, 77);

    assert(memcmp(&first, &second, sizeof(first)) == 0);
    assert(first.sample[0].time_rt == 500);
    assert(first.sample[0].weight == 1);
    assert(first.oldest_sample == 0);
    assert(first.sum_weighted_squares == UINT64_C(250000));
    assert(first.sum_weighted_times == 500);
    assert(first.sum_weight == 1);
    assert(first.weighted_avg == 500);
    assert(first.std_deviation == 0);
}

static void test_sample_weighting(void)
{
    timeout_t timeout;

    timeout_init(&timeout, 500, 1);
    timeout_add_sample(&timeout, 100, 1);
    assert(timeout.sample[0].time_rt == 100);
    assert(timeout.sample[0].weight == 1023);
    assert(timeout.oldest_sample == 1);
    assert(timeout.weighted_avg == 100);
    assert(timeout.std_deviation == 0);

    timeout_add_sample(&timeout, 200, 1);
    assert(timeout.weighted_avg == 150);
    assert(timeout.std_deviation == 50);

    timeout_init(&timeout, 500, 1);
    timeout_add_sample(&timeout, 100, 1);
    timeout_add_sample(&timeout, 1000, 1023);
    assert(timeout.sample[1].weight == 1);
    assert(timeout.weighted_avg == 100);
    assert(timeout.std_deviation == 29);
}

static void test_sample_clamp(void)
{
    timeout_t timeout;

    timeout_init(&timeout, UINT32_C(70000), 1);
    assert(timeout.sample[0].time_rt == UINT16_MAX);
    assert(timeout.weighted_avg == UINT16_MAX);

    timeout_add_sample(&timeout, UINT32_C(70000), 1);
    assert(timeout.sample[0].time_rt == UINT16_MAX);
    assert(timeout.weighted_avg == UINT16_MAX);
    assert(timeout.std_deviation == 0);
}

static void test_sample_ring(void)
{
    timeout_t timeout;
    uint32_t index;

    timeout_init(&timeout, 500, 1);
    for (index = 0; index < 64; ++index)
    {
        timeout_add_sample(&timeout, 100, 1);
    }

    assert(timeout.oldest_sample == 0);
    assert(timeout.sum_weight == 64u * 1023u);
    assert(timeout.weighted_avg == 100);
    assert(timeout.std_deviation == 0);

    timeout_add_sample(&timeout, 200, 1);
    assert(timeout.oldest_sample == 1);
    assert(timeout.sample[0].time_rt == 200);
    assert(timeout.weighted_avg == 101);
}

static void test_timeout_clamps(void)
{
    timeout_t timeout;

    memset(&timeout, 0, sizeof(timeout));
    assert(timeout_get_timeout(&timeout) == 50);
    assert(timeout_get_ancient(&timeout) == 50);

    timeout.weighted_avg = 100;
    timeout.std_deviation = 20;
    assert(timeout_get_timeout(&timeout) == 140);
    assert(timeout_get_ancient(&timeout) == 160);

    timeout.weighted_avg = 65000;
    timeout.std_deviation = 300;
    assert(timeout_get_timeout(&timeout) == UINT16_MAX);
    assert(timeout_get_ancient(&timeout) == UINT16_MAX);
}

static void test_variance_arithmetic(void)
{
    timeout_t timeout;
    uint32_t index;

    timeout_init(&timeout, 500, 1);
    timeout_add_sample(&timeout, 100, 1);
    timeout_add_sample(&timeout, 3000, 1);

    assert(timeout.weighted_avg == 1550);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(timeout.sum_weighted_squares == UINT64_C(627295408));
    assert(timeout.std_deviation == 57);
    assert(timeout_get_timeout(&timeout) == 1664);
    assert(timeout_get_ancient(&timeout) == 1721);
#else
    assert(timeout.sum_weighted_squares == UINT64_C(9217230000));
    assert(timeout.std_deviation == 1450);
    assert(timeout_get_timeout(&timeout) == 4450);
    assert(timeout_get_ancient(&timeout) == 5900);
#endif

    timeout_init(&timeout, 500, 1);
    for (index = 0; index < 64; ++index)
    {
        timeout_add_sample(&timeout, 300, 1);
    }

    assert(timeout.weighted_avg == 300);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(timeout.std_deviation == 256);
    assert(timeout_get_timeout(&timeout) == 812);
    assert(timeout_get_ancient(&timeout) == 1068);
#else
    assert(timeout.std_deviation == 0);
    assert(timeout_get_timeout(&timeout) == 300);
    assert(timeout_get_ancient(&timeout) == 300);
#endif
}

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
static void test_checked_arguments(void)
{
    timeout_t timeout;
    timeout_t snapshot;

    timeout_init(NULL, 500, 1);
    timeout_add_sample(NULL, 100, 1);

    timeout_init(&timeout, 500, 1);
    memcpy(&snapshot, &timeout, sizeof(snapshot));
    timeout_add_sample(&timeout, 100, 0);
    assert(memcmp(&timeout, &snapshot, sizeof(timeout)) == 0);
}
#endif

int main(void)
{
    test_signatures_and_layout();
    test_initialization();
    test_sample_weighting();
    test_sample_clamp();
    test_sample_ring();
    test_timeout_clamps();
    test_variance_arithmetic();
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    test_checked_arguments();
#endif
    return 0;
}
