// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef RDPLIB_SOURCE_FAITHFUL

#include "test_assert.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log.h"

_Static_assert(_Generic(&time_format, void (*)(char *): 1, default: 0), "time_format signature");
_Static_assert(_Generic(&ftimeprint, void (*)(FILE *): 1, default: 0), "ftimeprint signature");
_Static_assert(_Generic(&discard_log_append, void (*)(char *, ...): 1, default: 0), "discard_log_append signature");

enum
{
    LOG_TIMESTAMP_LENGTH = 20
};

static int timestamp_has_expected_shape(const char *timestamp)
{
    static const size_t digit_positions[] = {1, 2, 4, 5, 7, 8, 10, 11, 13, 14, 16, 17};
    size_t index;

    if (strlen(timestamp) != LOG_TIMESTAMP_LENGTH || timestamp[0] != '[' || timestamp[3] != '/' || timestamp[6] != '/' || timestamp[9] != ' ' || timestamp[12] != ':' || timestamp[15] != ':' ||
        timestamp[18] != ']' || timestamp[19] != ' ')
    {
        return 0;
    }

    for (index = 0; index < sizeof(digit_positions) / sizeof(digit_positions[0]); ++index)
    {
        if (!isdigit((unsigned char)timestamp[digit_positions[index]]))
        {
            return 0;
        }
    }

    return 1;
}

static void format_utc_timestamp(char output[LOG_TIMESTAMP_LENGTH + 1], time_t value)
{
    struct tm *utc;

    utc = gmtime(&value);
    assert(utc != NULL);
    assert(snprintf(output, LOG_TIMESTAMP_LENGTH + 1, "[%02u/%02u/%02u %02u:%02u:%02u] ", (unsigned)utc->tm_mon + 1u, (unsigned)utc->tm_mday, (unsigned)(utc->tm_year % 100), (unsigned)utc->tm_hour,
                    (unsigned)utc->tm_min, (unsigned)utc->tm_sec) == LOG_TIMESTAMP_LENGTH);
}

static int timestamp_matches_utc_interval(const char *timestamp, time_t first, time_t last)
{
    char expected[LOG_TIMESTAMP_LENGTH + 1];
    time_t value;

    assert(last >= first);
    assert(last - first <= 60);

    for (value = first;; ++value)
    {
        format_utc_timestamp(expected, value);
        if (strcmp(timestamp, expected) == 0)
        {
            return 1;
        }
        if (value == last)
        {
            break;
        }
    }

    return 0;
}

static void test_time_format(void)
{
    char timestamp[LOG_TIMESTAMP_LENGTH + 1];
    time_t first;
    time_t last;

    first = time(NULL);
    time_format(timestamp);
    last = time(NULL);

    assert(first != (time_t)-1);
    assert(last != (time_t)-1);
    assert(timestamp_has_expected_shape(timestamp));
    assert(timestamp_matches_utc_interval(timestamp, first, last));
}

static void test_ftimeprint(void)
{
    char timestamp[LOG_TIMESTAMP_LENGTH + 2];
    FILE *file;
    size_t bytes_read;
    time_t first;
    time_t last;

    file = tmpfile();
    assert(file != NULL);

    first = time(NULL);
    ftimeprint(file);
    last = time(NULL);

    assert(first != (time_t)-1);
    assert(last != (time_t)-1);
    assert(fflush(file) == 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    bytes_read = fread(timestamp, 1, sizeof(timestamp) - 1, file);
    assert(bytes_read == LOG_TIMESTAMP_LENGTH);
    timestamp[bytes_read] = '\0';
    assert(timestamp_has_expected_shape(timestamp));
    assert(timestamp_matches_utc_interval(timestamp, first, last));
    assert(fclose(file) == 0);
}

static long file_size_or_zero(const char *path)
{
    FILE *file;
    long size;

    file = fopen(path, "rb");
    if (!file)
    {
        return 0;
    }
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size >= 0);
    assert(fclose(file) == 0);
    return size;
}

static void test_discard_log_append(void)
{
#ifdef _WIN32
    static const char path[] = "discard.log";
    static const char file_message[] = "log audit 37\r\n";
#else
    static const char path[] = "Logs:discard.log";
    static const char file_message[] = "log audit 37\n";
#endif
    char appended[LOG_TIMESTAMP_LENGTH + sizeof(file_message)];
    FILE *file;
    long original_size;
    size_t bytes_read;
    time_t first;
    time_t last;

    original_size = file_size_or_zero(path);
    first = time(NULL);
    discard_log_append("log audit %u\n", 37u);
    last = time(NULL);

    assert(first != (time_t)-1);
    assert(last != (time_t)-1);
    file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, original_size, SEEK_SET) == 0);
    bytes_read = fread(appended, 1, sizeof(appended) - 1, file);
    assert(bytes_read == LOG_TIMESTAMP_LENGTH + sizeof(file_message) - 1);
    appended[bytes_read] = '\0';
    appended[LOG_TIMESTAMP_LENGTH] = '\0';
    assert(timestamp_has_expected_shape(appended));
    assert(timestamp_matches_utc_interval(appended, first, last));
    appended[LOG_TIMESTAMP_LENGTH] = file_message[0];
    assert(strcmp(appended + LOG_TIMESTAMP_LENGTH, file_message) == 0);
    assert(fclose(file) == 0);
}

#endif

int main(void)
{
#ifdef RDPLIB_SOURCE_FAITHFUL
    test_time_format();
    test_ftimeprint();
    test_discard_log_append();
#endif
    return 0;
}
