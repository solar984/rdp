// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "rdplib.h"

enum
{
    TEST_CONNECTION_COUNT = 2,
    TEST_SENDER_COUNT = 4,
    TEST_MESSAGES_PER_SENDER = 32,
    TEST_CLOSE_PAYLOAD_BYTES = 51200,
    TEST_DATA_RATE = 16u * 1024u * 1024u,
    TEST_SEND_BUFFER_BYTES = 4u * 1024u * 1024u,
    TEST_MAGIC = UINT32_C(0x4d534e44),
    TEST_PROBE_MAGIC = UINT32_C(0x50524f42),
    TEST_CLOSE_MAGIC = UINT32_C(0x434c5344)
};

typedef struct start_gate_t
{
    mtx_t mutex;
    cnd_t condition;
    uint32_t participants;
    uint32_t waiting;
    int open;
} start_gate_t;

typedef struct message_payload_t
{
    uint32_t magic;
    uint32_t connection_index;
    uint32_t sender_index;
    uint32_t sequence;
    uint8_t padding[48];
} message_payload_t;

typedef struct sender_context_t
{
    start_gate_t *gate;
    rdplib_connection_t *connection;
    uint32_t connection_index;
    uint32_t sender_index;
    int result;
} sender_context_t;

typedef struct operation_state_t
{
    mtx_t mutex;
    cnd_t condition;
    int started;
    int completed;
} operation_state_t;

typedef struct blocking_callback_t
{
    mtx_t mutex;
    cnd_t condition;
    const uint8_t *match;
    uint32_t match_bytes;
    int entered;
    int released;
} blocking_callback_t;

typedef struct single_send_context_t
{
    operation_state_t *state;
    rdplib_connection_t *connection;
    const void *data;
    uint32_t bytes;
    uint32_t stream;
    int result;
} single_send_context_t;

typedef struct close_context_t
{
    operation_state_t *state;
    rdplib_connection_t *connection;
    int result;
} close_context_t;

typedef struct receive_state_t
{
    uint32_t next_sequence[TEST_SENDER_COUNT];
    uint32_t received;
} receive_state_t;

static uint8_t close_payload[TEST_CLOSE_PAYLOAD_BYTES];

static void start_gate_init(start_gate_t *gate, uint32_t participants)
{
    memset(gate, 0, sizeof(*gate));
    assert(mtx_init(&gate->mutex, mtx_plain) == thrd_success);
    assert(cnd_init(&gate->condition) == thrd_success);
    gate->participants = participants;
}

static void start_gate_wait(start_gate_t *gate)
{
    assert(mtx_lock(&gate->mutex) == thrd_success);
    ++gate->waiting;
    assert(cnd_broadcast(&gate->condition) == thrd_success);
    while (!gate->open)
    {
        assert(cnd_wait(&gate->condition, &gate->mutex) == thrd_success);
    }
    assert(mtx_unlock(&gate->mutex) == thrd_success);
}

static void start_gate_open(start_gate_t *gate)
{
    assert(mtx_lock(&gate->mutex) == thrd_success);
    while (gate->waiting != gate->participants)
    {
        assert(cnd_wait(&gate->condition, &gate->mutex) == thrd_success);
    }
    gate->open = 1;
    assert(cnd_broadcast(&gate->condition) == thrd_success);
    assert(mtx_unlock(&gate->mutex) == thrd_success);
}

static void start_gate_destroy(start_gate_t *gate)
{
    cnd_destroy(&gate->condition);
    mtx_destroy(&gate->mutex);
}

static void operation_state_init(operation_state_t *state)
{
    memset(state, 0, sizeof(*state));
    assert(mtx_init(&state->mutex, mtx_plain) == thrd_success);
    assert(cnd_init(&state->condition) == thrd_success);
}

static void operation_state_mark_started(operation_state_t *state)
{
    assert(mtx_lock(&state->mutex) == thrd_success);
    state->started = 1;
    assert(cnd_broadcast(&state->condition) == thrd_success);
    assert(mtx_unlock(&state->mutex) == thrd_success);
}

static void operation_state_mark_completed(operation_state_t *state)
{
    assert(mtx_lock(&state->mutex) == thrd_success);
    state->completed = 1;
    assert(cnd_broadcast(&state->condition) == thrd_success);
    assert(mtx_unlock(&state->mutex) == thrd_success);
}

static int operation_state_wait(operation_state_t *state, int wait_for_completion, time_t timeout_seconds)
{
    struct timespec deadline;
    int reached;
    int wait_result = thrd_success;

    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    deadline.tv_sec += timeout_seconds;
    assert(mtx_lock(&state->mutex) == thrd_success);
    reached = wait_for_completion ? state->completed : state->started;
    while (!reached && wait_result == thrd_success)
    {
        wait_result = cnd_timedwait(&state->condition, &state->mutex, &deadline);
        reached = wait_for_completion ? state->completed : state->started;
    }
    assert(wait_result == thrd_success || wait_result == thrd_timedout);
    assert(mtx_unlock(&state->mutex) == thrd_success);
    return reached;
}

static int operation_state_is_completed(operation_state_t *state)
{
    int completed;

    assert(mtx_lock(&state->mutex) == thrd_success);
    completed = state->completed;
    assert(mtx_unlock(&state->mutex) == thrd_success);
    return completed;
}

static void operation_state_destroy(operation_state_t *state)
{
    cnd_destroy(&state->condition);
    mtx_destroy(&state->mutex);
}

static void blocking_callback_init(blocking_callback_t *callback, const void *match, uint32_t match_bytes)
{
    memset(callback, 0, sizeof(*callback));
    assert(mtx_init(&callback->mutex, mtx_plain) == thrd_success);
    assert(cnd_init(&callback->condition) == thrd_success);
    callback->match = (const uint8_t *)match;
    callback->match_bytes = match_bytes;
}

static int blocking_callback_wait_until_entered(blocking_callback_t *callback, time_t timeout_seconds)
{
    struct timespec deadline;
    int wait_result = thrd_success;

    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    deadline.tv_sec += timeout_seconds;
    assert(mtx_lock(&callback->mutex) == thrd_success);
    while (!callback->entered && wait_result == thrd_success)
    {
        wait_result = cnd_timedwait(&callback->condition, &callback->mutex, &deadline);
    }
    assert(wait_result == thrd_success || wait_result == thrd_timedout);
    assert(mtx_unlock(&callback->mutex) == thrd_success);
    return callback->entered;
}

static void blocking_callback_release(blocking_callback_t *callback)
{
    assert(mtx_lock(&callback->mutex) == thrd_success);
    callback->released = 1;
    assert(cnd_broadcast(&callback->condition) == thrd_success);
    assert(mtx_unlock(&callback->mutex) == thrd_success);
}

static void blocking_callback_destroy(blocking_callback_t *callback)
{
    cnd_destroy(&callback->condition);
    mtx_destroy(&callback->mutex);
}

static int block_first_outbound_packet(void *argument, rdplib_packet_drop_direction_t direction, const uint8_t *packet, uint32_t packet_bytes)
{
    blocking_callback_t *callback = (blocking_callback_t *)argument;
    uint32_t offset;
    int matches = 0;

    if (direction != RDPLIB_PACKET_DROP_OUTBOUND || packet_bytes < callback->match_bytes)
    {
        return 0;
    }
    for (offset = 0; offset <= packet_bytes - callback->match_bytes; ++offset)
    {
        if (memcmp(packet + offset, callback->match, callback->match_bytes) == 0)
        {
            matches = 1;
            break;
        }
    }
    if (!matches)
    {
        return 0;
    }

    assert(mtx_lock(&callback->mutex) == thrd_success);
    if (!callback->entered)
    {
        callback->entered = 1;
        assert(cnd_broadcast(&callback->condition) == thrd_success);
        while (!callback->released)
        {
            assert(cnd_wait(&callback->condition, &callback->mutex) == thrd_success);
        }
    }
    assert(mtx_unlock(&callback->mutex) == thrd_success);
    return 0;
}

static int sender_thread(void *argument)
{
    sender_context_t *context = (sender_context_t *)argument;
    message_payload_t payload;
    uint32_t sequence;

    memset(&payload, 0, sizeof(payload));
    payload.magic = TEST_MAGIC;
    payload.connection_index = context->connection_index;
    payload.sender_index = context->sender_index;
    memset(payload.padding, (int)(context->sender_index + 1u), sizeof(payload.padding));

    context->result = RDPLIB_CONNECTION_SEND_OK;
    start_gate_wait(context->gate);
    for (sequence = 0; sequence < TEST_MESSAGES_PER_SENDER; ++sequence)
    {
        payload.sequence = sequence;
        context->result = rdplib_connection_send(context->connection, &payload, (uint32_t)sizeof(payload), 1, RDPLIB_SEND_RELIABLE);
        if (context->result != RDPLIB_CONNECTION_SEND_OK)
        {
            break;
        }
    }
    return 0;
}

static int single_send_thread(void *argument)
{
    single_send_context_t *context = (single_send_context_t *)argument;

    operation_state_mark_started(context->state);
    context->result = rdplib_connection_send(context->connection, context->data, context->bytes, context->stream, RDPLIB_SEND_RELIABLE);
    operation_state_mark_completed(context->state);
    return 0;
}

static int close_thread(void *argument)
{
    close_context_t *context = (close_context_t *)argument;

    operation_state_mark_started(context->state);
    context->result = rdplib_connection_begin_close(context->connection, 5000);
    operation_state_mark_completed(context->state);
    return 0;
}

static void drain_regular_messages(rdplib_connection_t *connection, uint32_t expected_connection_index, receive_state_t *state)
{
    rdplib_message_t *message;

    while ((message = rdplib_connection_pop_message(connection)) != NULL)
    {
        message_payload_t payload;

        assert(!rdplib_message_has_fin(message));
        assert(!rdplib_message_is_disconnect(message));
        assert(rdplib_message_size(message) == sizeof(payload));
        memcpy(&payload, rdplib_message_data(message), sizeof(payload));
        assert(payload.magic == TEST_MAGIC);
        assert(payload.connection_index == expected_connection_index);
        assert(payload.sender_index < TEST_SENDER_COUNT);
        assert(payload.sequence == state->next_sequence[payload.sender_index]);
        ++state->next_sequence[payload.sender_index];
        ++state->received;
        rdplib_message_release(message);
    }
}

static void drain_close_messages(rdplib_connection_t *connection, int *saw_payload, int *saw_fin)
{
    rdplib_message_t *message;

    while ((message = rdplib_connection_pop_message(connection)) != NULL)
    {
        if (rdplib_message_has_fin(message))
        {
            *saw_fin = 1;
        }
        else
        {
            uint32_t magic;

            assert(!rdplib_message_is_disconnect(message));
            assert(rdplib_message_size(message) == TEST_CLOSE_PAYLOAD_BYTES);
            memcpy(&magic, rdplib_message_data(message), sizeof(magic));
            assert(magic == TEST_CLOSE_MAGIC);
            assert(!*saw_payload);
            *saw_payload = 1;
        }
        rdplib_message_release(message);
    }
}

static void drain_probe_message(rdplib_connection_t *connection, int *saw_probe)
{
    rdplib_message_t *message;

    while ((message = rdplib_connection_pop_message(connection)) != NULL)
    {
        uint32_t magic;

        assert(!rdplib_message_has_fin(message));
        assert(!rdplib_message_is_disconnect(message));
        assert(rdplib_message_size(message) == sizeof(magic));
        memcpy(&magic, rdplib_message_data(message), sizeof(magic));
        assert(magic == TEST_PROBE_MAGIC);
        assert(!*saw_probe);
        *saw_probe = 1;
        rdplib_message_release(message);
    }
}

static void drain_until_fin(rdplib_endpoint_t *endpoint, rdplib_connection_t *connection)
{
    int saw_fin = 0;
    int attempt;

    for (attempt = 0; attempt < 1000 && !saw_fin; ++attempt)
    {
        rdplib_message_t *message;

        assert(rdplib_endpoint_process(endpoint, 5) >= 0);
        while ((message = rdplib_connection_pop_message(connection)) != NULL)
        {
            saw_fin = saw_fin || rdplib_message_has_fin(message);
            rdplib_message_release(message);
        }
    }
    assert(saw_fin);
}

static void connect_client(rdplib_endpoint_t *server, rdplib_endpoint_t *client, uint32_t connection_index, rdplib_connection_t **client_connection, rdplib_connection_t **server_connection)
{
    message_payload_t bootstrap;
    rdplib_message_t *message;

    memset(&bootstrap, 0, sizeof(bootstrap));
    bootstrap.magic = TEST_MAGIC;
    bootstrap.connection_index = connection_index;
    bootstrap.sender_index = UINT32_MAX;

    assert(rdplib_connect(client, client_connection, "127.0.0.1", rdplib_endpoint_local_port(server)) == RDPLIB_CONNECT_OK);
    assert(rdplib_connection_set_data_rate(*client_connection, TEST_DATA_RATE) == RDPLIB_OK);
    assert(rdplib_connection_set_send_buffer_size(*client_connection, TEST_SEND_BUFFER_BYTES) == RDPLIB_OK);
    assert(rdplib_connection_send(*client_connection, &bootstrap, (uint32_t)sizeof(bootstrap), 1, RDPLIB_SEND_RELIABLE) == RDPLIB_CONNECTION_SEND_OK);
    assert(rdplib_endpoint_process(server, 5000) > 0);

    *server_connection = rdplib_endpoint_accept(server);
    assert(*server_connection != NULL);
    assert(rdplib_connection_set_data_rate(*server_connection, TEST_DATA_RATE) == RDPLIB_OK);
    assert(rdplib_connection_set_send_buffer_size(*server_connection, TEST_SEND_BUFFER_BYTES) == RDPLIB_OK);

    message = rdplib_connection_pop_message(*server_connection);
    assert(message != NULL);
    assert(rdplib_message_size(message) == sizeof(bootstrap));
    memcpy(&bootstrap, rdplib_message_data(message), sizeof(bootstrap));
    assert(bootstrap.magic == TEST_MAGIC);
    assert(bootstrap.connection_index == connection_index);
    rdplib_message_release(message);
}

int main(void)
{
    rdplib_runtime_t *runtime = NULL;
    rdplib_endpoint_t *server = NULL;
    rdplib_endpoint_t *clients[TEST_CONNECTION_COUNT] = {NULL};
    rdplib_connection_t *client_connections[TEST_CONNECTION_COUNT] = {NULL};
    rdplib_connection_t *server_connections[TEST_CONNECTION_COUNT] = {NULL};
    start_gate_t send_gate;
    sender_context_t sender_contexts[TEST_SENDER_COUNT];
    thrd_t sender_threads[TEST_SENDER_COUNT];
    receive_state_t receive_state;
    uint32_t expected_messages = TEST_SENDER_COUNT * TEST_MESSAGES_PER_SENDER;
    uint32_t sender_index;
    uint32_t connection_index;
    int attempt;

    memset(&receive_state, 0, sizeof(receive_state));
    assert(rdplib_runtime_create(&runtime, 4u * 1024u * 1024u) == RDPLIB_OK);
    assert(rdplib_endpoint_create(runtime, &server, 0, TEST_CONNECTION_COUNT, RDPLIB_USE_CRC) == RDPLIB_ENDPOINT_CREATE_OK);
    for (connection_index = 0; connection_index < TEST_CONNECTION_COUNT; ++connection_index)
    {
        assert(rdplib_endpoint_create(runtime, &clients[connection_index], 0, 1, RDPLIB_USE_CRC) == RDPLIB_ENDPOINT_CREATE_OK);
        connect_client(server, clients[connection_index], connection_index, &client_connections[connection_index], &server_connections[connection_index]);
    }

    start_gate_init(&send_gate, TEST_SENDER_COUNT);
    for (sender_index = 0; sender_index < TEST_SENDER_COUNT; ++sender_index)
    {
        sender_context_t *context = &sender_contexts[sender_index];

        memset(context, 0, sizeof(*context));
        context->gate = &send_gate;
        context->connection_index = sender_index / 2u;
        context->sender_index = sender_index;
        context->connection = server_connections[context->connection_index];
        assert(thrd_create(&sender_threads[sender_index], sender_thread, context) == thrd_success);
    }
    start_gate_open(&send_gate);

    for (attempt = 0; attempt < 1000 && receive_state.received != expected_messages; ++attempt)
    {
        for (connection_index = 0; connection_index < TEST_CONNECTION_COUNT; ++connection_index)
        {
            assert(rdplib_endpoint_process(clients[connection_index], 5) >= 0);
            drain_regular_messages(client_connections[connection_index], connection_index, &receive_state);
        }
    }
    for (sender_index = 0; sender_index < TEST_SENDER_COUNT; ++sender_index)
    {
        int thread_result;

        assert(thrd_join(sender_threads[sender_index], &thread_result) == thrd_success);
        assert(thread_result == 0);
        assert(sender_contexts[sender_index].result == RDPLIB_CONNECTION_SEND_OK);
        assert(receive_state.next_sequence[sender_index] == TEST_MESSAGES_PER_SENDER);
    }
    assert(receive_state.received == expected_messages);
    start_gate_destroy(&send_gate);

    {
        blocking_callback_t callback;
        operation_state_t close_send_state;
        operation_state_t independent_send_state;
        operation_state_t close_state;
        single_send_context_t close_send_context;
        single_send_context_t independent_send_context;
        close_context_t close_context;
        thrd_t close_sender;
        thrd_t independent_sender;
        thrd_t closer;
        uint32_t close_magic = TEST_CLOSE_MAGIC;
        uint32_t probe_magic = TEST_PROBE_MAGIC;
        int callback_result;
        int saw_payload = 0;
        int saw_fin = 0;
        int saw_probe = 0;

        memset(close_payload, 0x5a, sizeof(close_payload));
        memcpy(close_payload, &close_magic, sizeof(close_magic));
        blocking_callback_init(&callback, close_payload, 16);
        callback_result = rdplib_connection_set_packet_drop_callback(server_connections[0], block_first_outbound_packet, &callback);
        if (callback_result == RDPLIB_OK)
        {
            struct timespec scheduling_window = {0, 20 * 1000 * 1000};
            int thread_result;

            operation_state_init(&close_send_state);
            memset(&close_send_context, 0, sizeof(close_send_context));
            close_send_context.state = &close_send_state;
            close_send_context.connection = server_connections[0];
            close_send_context.data = close_payload;
            close_send_context.bytes = TEST_CLOSE_PAYLOAD_BYTES;
            close_send_context.stream = 2;
            assert(thrd_create(&close_sender, single_send_thread, &close_send_context) == thrd_success);
            assert(operation_state_wait(&close_send_state, 0, 5));
            assert(blocking_callback_wait_until_entered(&callback, 5));
            assert(!operation_state_is_completed(&close_send_state));

            operation_state_init(&independent_send_state);
            memset(&independent_send_context, 0, sizeof(independent_send_context));
            independent_send_context.state = &independent_send_state;
            independent_send_context.connection = server_connections[1];
            independent_send_context.data = &probe_magic;
            independent_send_context.bytes = (uint32_t)sizeof(probe_magic);
            independent_send_context.stream = 2;
            assert(thrd_create(&independent_sender, single_send_thread, &independent_send_context) == thrd_success);
            assert(operation_state_wait(&independent_send_state, 1, 5));
            assert(thrd_join(independent_sender, &thread_result) == thrd_success);
            assert(thread_result == 0);
            assert(independent_send_context.result == RDPLIB_CONNECTION_SEND_OK);

            operation_state_init(&close_state);
            memset(&close_context, 0, sizeof(close_context));
            close_context.state = &close_state;
            close_context.connection = server_connections[0];
            assert(thrd_create(&closer, close_thread, &close_context) == thrd_success);
            assert(operation_state_wait(&close_state, 0, 5));
            (void)thrd_sleep(&scheduling_window, NULL);
            assert(!operation_state_is_completed(&close_state));

            blocking_callback_release(&callback);
            assert(thrd_join(close_sender, &thread_result) == thrd_success);
            assert(thread_result == 0);
            assert(close_send_context.result == RDPLIB_CONNECTION_SEND_OK);
            assert(thrd_join(closer, &thread_result) == thrd_success);
            assert(thread_result == 0);
            assert(close_context.result == RDPLIB_OK);

            operation_state_destroy(&close_state);
            operation_state_destroy(&independent_send_state);
            operation_state_destroy(&close_send_state);
        }
        else
        {
            assert(callback_result == RDPLIB_ERROR_NOT_SUPPORTED);
            assert(rdplib_connection_send(server_connections[0], close_payload, TEST_CLOSE_PAYLOAD_BYTES, 2, RDPLIB_SEND_RELIABLE) == RDPLIB_CONNECTION_SEND_OK);
            assert(rdplib_connection_send(server_connections[1], &probe_magic, (uint32_t)sizeof(probe_magic), 2, RDPLIB_SEND_RELIABLE) == RDPLIB_CONNECTION_SEND_OK);
            assert(rdplib_connection_begin_close(server_connections[0], 5000) == RDPLIB_OK);
        }
        blocking_callback_destroy(&callback);
        assert(rdplib_connection_send(server_connections[0], close_payload, TEST_CLOSE_PAYLOAD_BYTES, 2, RDPLIB_SEND_RELIABLE) == RDPLIB_ERROR_NOT_USABLE);

        for (attempt = 0; attempt < 2000 && (!saw_payload || !saw_fin || !saw_probe); ++attempt)
        {
            assert(rdplib_endpoint_process(clients[0], 5) >= 0);
            drain_close_messages(client_connections[0], &saw_payload, &saw_fin);
            assert(rdplib_endpoint_process(clients[1], 5) >= 0);
            drain_probe_message(client_connections[1], &saw_probe);
        }
        assert(saw_payload);
        assert(saw_fin);
        assert(saw_probe);

        rdplib_connection_release(server_connections[0]);
        server_connections[0] = NULL;
        rdplib_connection_release(client_connections[0]);
        client_connections[0] = NULL;
    }

    assert(rdplib_connection_begin_close(server_connections[1], 1000) == RDPLIB_OK);
    drain_until_fin(clients[1], client_connections[1]);
    rdplib_connection_release(server_connections[1]);
    server_connections[1] = NULL;
    rdplib_connection_release(client_connections[1]);
    client_connections[1] = NULL;

    for (connection_index = 0; connection_index < TEST_CONNECTION_COUNT; ++connection_index)
    {
        assert(rdplib_endpoint_destroy(clients[connection_index]) == RDPLIB_OK);
        clients[connection_index] = NULL;
    }
    assert(rdplib_endpoint_destroy(server) == RDPLIB_OK);
    assert(rdplib_runtime_destroy(runtime) == RDPLIB_OK);
    return 0;
}
