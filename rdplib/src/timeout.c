// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "timeout.h"

#include <math.h>
#include <string.h>

enum
{
    RDP_TIMEOUT_SAMPLE_COUNT = 64,
    RDP_TIMEOUT_WEIGHT_NUMERATOR = 1023
};

void timeout_init(_timeout_t *state, uint32_t initial_milliseconds, uint16_t initial_weight)
{
    uint16_t initial_sample;

    (void)initial_weight;
    if (!state)
    {
        return;
    }

    memset(state, 0, sizeof(*state));
    initial_sample = initial_milliseconds > UINT16_MAX ? UINT16_MAX : (uint16_t)initial_milliseconds;
    state->samples[0].milliseconds = initial_sample;
    state->samples[0].weight = 1;
    state->sum_squares = (uint64_t)initial_sample * initial_sample;
    state->weighted_sum = initial_sample;
    state->total_weight = 1;
    state->mean_ms = initial_sample;
}

void timeout_add_sample(_timeout_t *state, uint32_t milliseconds, uint16_t transmission_count)
{
    rdp_timeout_sample_t *sample;
    uint32_t weighted_value;
    uint32_t weight;

    if (!state || !transmission_count)
    {
        return;
    }

    sample = &state->samples[state->next_sample];

    weighted_value = (uint32_t)sample->milliseconds * sample->weight;
    state->weighted_sum -= weighted_value;
    state->sum_squares -= (uint64_t)weighted_value * sample->milliseconds;
    state->total_weight = (uint16_t)(state->total_weight - sample->weight);

    sample->milliseconds = milliseconds > UINT16_MAX ? UINT16_MAX : (uint16_t)milliseconds;
    weight = RDP_TIMEOUT_WEIGHT_NUMERATOR / transmission_count;
    sample->weight = (uint16_t)(weight ? weight : 1u);

    weighted_value = (uint32_t)sample->milliseconds * sample->weight;
    state->weighted_sum += weighted_value;
    state->sum_squares += (uint64_t)weighted_value * sample->milliseconds;
    state->total_weight = (uint16_t)(state->total_weight + sample->weight);

    state->mean_ms = (uint16_t)(state->weighted_sum / state->total_weight);
    if (state->total_weight <= 1)
    {
        state->deviation_ms = 0;
    }
    else
    {
        uint64_t squared_error_sum = state->sum_squares - (uint64_t)state->weighted_sum * state->mean_ms;
        double sample_variance = (double)squared_error_sum / (state->total_weight - 1u);
        state->deviation_ms = (uint16_t)sqrt(sample_variance);
    }

    state->next_sample = (state->next_sample + 1u) & (RDP_TIMEOUT_SAMPLE_COUNT - 1u);
}
