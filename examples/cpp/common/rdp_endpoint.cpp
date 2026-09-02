// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "rdp_endpoint.h"

#include "rdp_connection.h"

#include <cassert>
#include <new>

namespace
{
void ReleaseConnection(rdplib_connection_t *connection)
{
    if (connection == nullptr)
        return;

    rdplib_message_t *message;
    while ((message = rdplib_connection_pop_message(connection)) != nullptr)
        rdplib_message_release(message);

    (void)rdplib_connection_begin_close(connection, 0);
    rdplib_connection_release(connection);
}

}

RDPEndpoint::RDPEndpoint()
    : m_endpoint(nullptr)
{
}

RDPEndpoint::~RDPEndpoint()
{
    int result = Close();
    assert(result == RDPLIB_OK);
    (void)result;
}

int RDPEndpoint::Open(RDPRuntime &runtime, std::uint16_t local_port, std::uint32_t expected_connections, std::uint32_t flags)
{
    if (m_endpoint != nullptr)
        return RDPLIB_ERROR_BUSY;
    if (!runtime.IsOpen())
        return RDPLIB_ERROR_INVALID_ARGUMENT;

    return rdplib_endpoint_create(runtime.m_runtime, &m_endpoint, local_port, expected_connections, flags);
}

int RDPEndpoint::Open(RDPRuntime &runtime, std::uint16_t local_port, const rdplib_endpoint_options_t &options, std::uint32_t expected_connections,
                      std::uint32_t flags)
{
    if (m_endpoint != nullptr)
        return RDPLIB_ERROR_BUSY;
    if (!runtime.IsOpen())
        return RDPLIB_ERROR_INVALID_ARGUMENT;

    return rdplib_endpoint_create_ex(runtime.m_runtime, &m_endpoint, local_port, expected_connections, flags, &options);
}

int RDPEndpoint::Close()
{
    if (m_endpoint == nullptr)
        return RDPLIB_OK;

    int result = rdplib_endpoint_destroy(m_endpoint);
    if (result == RDPLIB_OK)
        m_endpoint = nullptr;

    return result;
}

int RDPEndpoint::Process(std::int32_t timeout_ms)
{
    if (m_endpoint == nullptr)
        return RDPLIB_ERROR_NOT_USABLE;

    int result = rdplib_endpoint_process(m_endpoint, timeout_ms);
    DiscardConnectionless();
    return result;
}

std::unique_ptr<RDPConnection> RDPEndpoint::Accept(int *result)
{
    if (result != nullptr)
        *result = RDPLIB_OK;

    if (m_endpoint == nullptr)
    {
        if (result != nullptr)
            *result = RDPLIB_ERROR_NOT_USABLE;
        return nullptr;
    }

    rdplib_connection_t *connection = rdplib_endpoint_accept(m_endpoint);
    if (connection == nullptr)
        return nullptr;

    return WrapConnection(connection, result);
}

std::unique_ptr<RDPConnection> RDPEndpoint::Connect(const char *host, std::uint16_t port, int *result)
{
    if (result != nullptr)
        *result = RDPLIB_OK;

    if (m_endpoint == nullptr || host == nullptr)
    {
        if (result != nullptr)
            *result = RDPLIB_ERROR_INVALID_ARGUMENT;
        return nullptr;
    }

    rdplib_connection_t *connection = nullptr;
    int connect_result = rdplib_connect(m_endpoint, &connection, host, port);
    if (connect_result != RDPLIB_OK)
    {
        if (result != nullptr)
            *result = connect_result;
        return nullptr;
    }

    return WrapConnection(connection, result);
}

std::uint16_t RDPEndpoint::LocalPort() const
{
    return m_endpoint != nullptr ? rdplib_endpoint_local_port(m_endpoint) : 0;
}

int RDPEndpoint::SetSocketReceiveBufferSize(std::uint32_t bytes)
{
    return rdplib_endpoint_set_socket_receive_buffer_size(m_endpoint, bytes);
}

int RDPEndpoint::SetSocketSendBufferSize(std::uint32_t bytes)
{
    return rdplib_endpoint_set_socket_send_buffer_size(m_endpoint, bytes);
}

int RDPEndpoint::GetSocketReceiveBufferSize(std::uint32_t &bytes) const
{
    return rdplib_endpoint_get_socket_receive_buffer_size(m_endpoint, &bytes);
}

int RDPEndpoint::GetSocketSendBufferSize(std::uint32_t &bytes) const
{
    return rdplib_endpoint_get_socket_send_buffer_size(m_endpoint, &bytes);
}

std::unique_ptr<RDPConnection> RDPEndpoint::WrapConnection(rdplib_connection_t *connection, int *result)
{
    std::unique_ptr<RDPConnection> wrapper(new (std::nothrow) RDPConnection(connection));
    if (wrapper == nullptr)
    {
        ReleaseConnection(connection);
        if (result != nullptr)
            *result = RDPLIB_ERROR_OUT_OF_MEMORY;
    }

    return wrapper;
}

void RDPEndpoint::DiscardConnectionless()
{
    if (m_endpoint == nullptr)
        return;

    rdplib_message_t *message;
    while ((message = rdplib_endpoint_pop_connectionless(m_endpoint)) != nullptr)
        rdplib_message_release(message);
}
