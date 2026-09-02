// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#ifndef RDP_EXAMPLE_APPLICATION_PACKET_H
#define RDP_EXAMPLE_APPLICATION_PACKET_H

#include <cstdint>
#include <vector>

// The application protocol uses a 2 byte little endian opcode followed by an
// opcode-specific payload.  RDP treats the complete buffer as one message.
class ApplicationPacket
{
public:
    ApplicationPacket();
    ApplicationPacket(std::uint16_t opcode, const void *payload = nullptr, std::uint32_t payload_bytes = 0);

    bool Decode(const void *data, std::uint32_t bytes);

    std::uint16_t Opcode() const;
    const std::uint8_t *Payload() const;
    std::uint32_t PayloadSize() const;

    const std::uint8_t *Data() const;
    std::uint32_t Size() const;

private:
    static constexpr std::uint32_t OpcodeSize = 2;

    std::uint16_t m_opcode;
    std::vector<std::uint8_t> m_buffer;
};

#endif // RDP_EXAMPLE_APPLICATION_PACKET_H
