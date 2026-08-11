// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "trace.h"

#include <stdio.h>
#include <string.h>

#include "rdplib_platform.h"

// Retained for historical interest, not used by rdplib

static char *format_trace_responder(char *output, const trace_sample_t *sample, int resolve_names)
{
    const uint8_t *address = (const uint8_t *)&sample->responder_ipv4;
    char numeric_address[16];
    char resolved_name[256];
    const char *name = NULL;

    if (resolve_names && rdplib_platform_reverse_ipv4(sample->responder_ipv4, resolved_name, sizeof(resolved_name)))
    {
        name = resolved_name;
    }

    if (name)
    {
        return output + sprintf(output, "%36s ", name);
    }

    sprintf(numeric_address, "%u.%u.%u.%u", address[0], address[1], address[2], address[3]);
    return output + sprintf(output, "%36s ", numeric_address);
}

static const char *format_trace_unreachable_suffix(uint8_t code)
{
    switch (code)
    {
    case 0:
        return "N";
    case 1:
        return "H";
    case 3:
        return "";
    case 6:
        return "NU";
    case 7:
        return "HU";
    default:
        return NULL;
    }
}

int format_trace(char *output, const trace_sample_t *samples, uint32_t sample_count, int resolve_names)
{
    char *cursor = output;
    uint32_t maximum_ttl = RDP_TRACE_MAX_TTL;
    uint32_t ttl;
    uint32_t index;

    for (index = 0; index < sample_count; ++index)
    {
        const trace_sample_t *sample = &samples[index];

        if (sample->icmp_type == 3 && sample->icmp_code == 3 && sample->ttl < maximum_ttl)
        {
            maximum_ttl = sample->ttl;
        }
    }

    for (ttl = 1; ttl <= maximum_ttl; ++ttl)
    {
        uint32_t previous_responder = 0;

        cursor += sprintf(cursor, " %2u ", ttl);
        for (index = 0; index < sample_count; ++index)
        {
            const trace_sample_t *sample = &samples[index];
            const char *suffix = NULL;

            if (sample->ttl != ttl)
            {
                continue;
            }

            if (sample->responder_ipv4 == 0)
            {
                strcpy(cursor, "   * ");
                cursor += 5;
                continue;
            }

            if (sample->responder_ipv4 != previous_responder)
            {
                if (previous_responder)
                {
                    strcpy(cursor, "\n    ");
                    cursor += 5;
                }
                cursor = format_trace_responder(cursor, sample, resolve_names);
            }

            if (sample->icmp_type == 3)
            {
                suffix = format_trace_unreachable_suffix(sample->icmp_code);
            }
            else if (sample->icmp_type == 11 && sample->icmp_code == 0)
            {
                suffix = "";
            }

            if (suffix)
            {
                cursor += sprintf(cursor, "%4u%s ", sample->round_trip_time_ms, suffix);
            }
            else
            {
                cursor += sprintf(cursor, "%u ICMP[%u][%u] ", sample->round_trip_time_ms, sample->icmp_type, sample->icmp_code);
            }
            previous_responder = sample->responder_ipv4;
        }

        *cursor++ = '\n';
        *cursor = '\0';
    }

    return (int)(cursor - output);
}
