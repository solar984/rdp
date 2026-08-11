// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDPLIB_PLATFORM_H
#define RDPLIB_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <winsock2.h>
#include <windows.h>

// rdplib deviation: rdplib_platform_* names are standalone build selected substitutions for operating system and embedding application services used by the recovered source.

typedef struct rdplib_platform_mutex_t
{
    CRITICAL_SECTION value;
    int initialized;
} rdplib_platform_mutex_t;
typedef OVERLAPPED rdplib_platform_serial_async_t;

static inline void rdplib_platform_serial_async_set_event(rdplib_platform_serial_async_t *async_state, intptr_t event)
{
    async_state->hEvent = (HANDLE)event;
}

static inline intptr_t rdplib_platform_serial_async_get_event(const rdplib_platform_serial_async_t *async_state)
{
    return (intptr_t)async_state->hEvent;
}

typedef struct rdplib_platform_semaphore_t
{
    HANDLE handle;
} rdplib_platform_semaphore_t;

typedef struct rdplib_platform_event_t
{
    HANDLE handle;
} rdplib_platform_event_t;

typedef uint32_t (*rdplib_platform_thread_entry_t)(void *argument);

#ifndef RDPLIB_SOURCE_FAITHFUL
enum
{
    RDPLIB_PLATFORM_RECEIVE_ICMP_PORT_UNREACHABLE = -2
};
#endif

#ifdef __cplusplus
extern "C"
{
#endif

// rdplib deviation: replaces the recovered malloc_override/free_override application hooks.
void *rdplib_platform_malloc(size_t size);
void rdplib_platform_free(void *allocation);

// rdplib deviation: A common mutex API replaces the source platform's pthread mutex or CRITICAL_SECTION operations.
void rdplib_platform_mutex_prepare(rdplib_platform_mutex_t *mutex);
void rdplib_platform_mutex_init(rdplib_platform_mutex_t *mutex);
void rdplib_platform_mutex_destroy(rdplib_platform_mutex_t *mutex);
void rdplib_platform_mutex_lock(rdplib_platform_mutex_t *mutex);
void rdplib_platform_mutex_unlock(rdplib_platform_mutex_t *mutex);

// Recovered usemaphore.obj boundary, selected for the host platform.
void rdplib_platform_semaphore_init(rdplib_platform_semaphore_t *semaphore);
int rdplib_platform_semaphore_create(rdplib_platform_semaphore_t *semaphore);
void rdplib_platform_semaphore_destroy(rdplib_platform_semaphore_t *semaphore);
int rdplib_platform_semaphore_wait(rdplib_platform_semaphore_t *semaphore, int32_t timeout_ms);
void rdplib_platform_semaphore_signal(rdplib_platform_semaphore_t *semaphore);

// Recovered uevent.obj boundary. Windows uses an auto reset event; the Mac lineage used a semaphore backed equivalent.
void rdplib_platform_event_init(rdplib_platform_event_t *event);
int rdplib_platform_event_create(rdplib_platform_event_t *event);
void rdplib_platform_event_destroy(rdplib_platform_event_t *event);
void rdplib_platform_event_signal(rdplib_platform_event_t *event);
int rdplib_platform_event_wait(rdplib_platform_event_t *event);

uint32_t rdplib_platform_current_time_ms(void);
// Recovered name: time_get_ms survives in both Mac symbol sets. This wrapper preserves the source era dependency.
uint32_t time_get_ms(void);
uint32_t rdplib_platform_wall_time_seconds(void);
uint32_t rdplib_platform_random_u32(void);
int rdplib_platform_resolve_ipv4(const char *host, uint16_t port, uint8_t endpoint[16]);

// rdplib deviation: source socket/library calls are normalized behind a host neutral 16 byte endpoint interface.
int rdplib_platform_network_startup(uint16_t version);
void rdplib_platform_network_cleanup(void);
uint16_t rdplib_platform_next_serial_port(void);
int rdplib_platform_protocol_number(const char *name, int fallback);
intptr_t rdplib_platform_socket_create(int family, int type, int protocol);
int rdplib_platform_socket_bind(intptr_t endpoint, const uint8_t *address, uint32_t address_bytes);
int rdplib_platform_socket_get_name(intptr_t endpoint, uint8_t *address, uint32_t *address_bytes);
int rdplib_platform_socket_set_option(intptr_t endpoint, int level, int option, const void *value, uint32_t value_bytes);
int rdplib_platform_socket_get_option(intptr_t endpoint, int level, int option, void *value, uint32_t *value_bytes);
int rdplib_platform_socket_disable_blocking(intptr_t endpoint);
uint32_t rdplib_platform_last_socket_error(void);
void rdplib_platform_record_socket_error(uint32_t error);
void rdplib_platform_socket_close(intptr_t endpoint);
int32_t rdplib_platform_send_datagram(intptr_t endpoint, const uint8_t *packet, uint32_t packet_bytes, const uint8_t destination[16]);
int32_t rdplib_platform_receive_datagram(intptr_t endpoint, uint8_t *packet, uint32_t packet_capacity, uint8_t source_address[16]);
#ifndef RDPLIB_SOURCE_FAITHFUL
int rdplib_platform_enable_icmp_errors(intptr_t endpoint);
#endif
int rdplib_platform_wait(intptr_t ipv4_socket, intptr_t legacy_socket, intptr_t icmp_socket, uint32_t enabled_sources, int infinite, uint32_t timeout_seconds, uint32_t timeout_microseconds,
                         uint32_t *ready_sources);
void rdplib_platform_sleep_ms(uint32_t milliseconds);
void rdplib_platform_send_wakeup(intptr_t ipv4_socket, const uint8_t local_address[16], uint32_t token);

// rdplib deviation: implements the recovered uthread_* family and its start_routine trampoline with native host threads.
int rdplib_platform_thread_create(void **thread, rdplib_platform_thread_entry_t entry, void *argument);
void rdplib_platform_thread_wait(void *thread, uint32_t *exit_code);
void rdplib_platform_thread_destroy(void *thread);
int rdplib_platform_reverse_ipv4(uint32_t address, char *name, uint32_t name_bytes);

// rdplib deviation: preserves the Windows overlapped serial behavior behind a build selected record; POSIX physical serial remains unavailable.
intptr_t rdplib_platform_serial_create_event(void);
void rdplib_platform_serial_close_event(intptr_t event);
int rdplib_platform_serial_write(intptr_t endpoint, rdplib_platform_serial_async_t *async_state, const void *data, uint32_t bytes, uint32_t *bytes_written, uint32_t *error_code);
int rdplib_platform_serial_get_write_result(intptr_t endpoint, rdplib_platform_serial_async_t *async_state, int wait, uint32_t *bytes_written, uint32_t *error_code);
int rdplib_platform_serial_read(intptr_t endpoint, rdplib_platform_serial_async_t *async_state, void *data, uint32_t bytes, uint32_t *bytes_read, uint32_t *error_code);
int rdplib_platform_serial_get_read_result(intptr_t endpoint, rdplib_platform_serial_async_t *async_state, int wait, uint32_t *bytes_read, uint32_t *error_code);

#ifdef __cplusplus
}
#endif

#endif /* RDPLIB_PLATFORM_H */
