// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDP_EXAMPLE_CONNECTION_H
#define RDP_EXAMPLE_CONNECTION_H

#include <cstdint>

#include "rdplib.h"

class RDPEndpoint;

// Owns one message returned by rdplib.  The payload remains valid until this
// object is destroyed or reused.
class RDPMessage
{
public:
    RDPMessage();
    ~RDPMessage();

    RDPMessage(const RDPMessage &) = delete;
    RDPMessage &operator=(const RDPMessage &) = delete;

    const std::uint8_t *Data() const;
    std::uint32_t Size() const;
    // Unsequenced messages have no stream field and report stream 0.
    std::uint8_t StreamNumber() const;
    std::uint16_t Flags() const;

private:
    friend class RDPConnection;

    void Reset(rdplib_message_t *message = nullptr);

    rdplib_message_t *m_message;
};

// Owns one application connection handle.  Its endpoint and runtime must
// outlive it.
class RDPConnection
{
public:
    enum ReceiveResult
    {
        NoData,
        MessageReceived,
        PeerClosed,
        ConnectionLost
    };

    ~RDPConnection();

    RDPConnection(const RDPConnection &) = delete;
    RDPConnection &operator=(const RDPConnection &) = delete;

    int EnableKeepalive();
    int SetDataRate(std::uint32_t bytes_per_second);
    int SetSendBufferSize(std::uint32_t bytes);

    // rdplib copies the borrowed data before returning.
    int Send(const void *data, std::uint32_t bytes, std::uint32_t stream, std::uint32_t flags);

    // This does not block.  MessageReceived transfers one message into message.
    ReceiveResult Receive(RDPMessage *message, std::uint32_t *disconnect_reason = nullptr);

    // Releases the application handle.  The endpoint retains a lingering
    // transport connection until it finishes or its deadline expires.
    void Close(std::uint32_t linger_timeout_ms);

    int GetRemoteAddress(std::uint8_t address[4], std::uint16_t &port) const;

private:
    friend class RDPEndpoint;

    explicit RDPConnection(rdplib_connection_t *connection);
    void DiscardMessages();

    rdplib_connection_t *m_connection;
    ReceiveResult m_terminal_result;
    std::uint32_t m_disconnect_reason;
};

#endif // RDP_EXAMPLE_CONNECTION_H
