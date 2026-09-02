// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "application_packet.h"

#include <cstring>

ApplicationPacket::ApplicationPacket()
    : m_opcode(0)
{
}

ApplicationPacket::ApplicationPacket(std::uint16_t opcode, const void *payload, std::uint32_t payload_bytes)
    : m_opcode(opcode),
      m_buffer(OpcodeSize + payload_bytes)
{
    m_buffer[0] = static_cast<std::uint8_t>(opcode & 0xffu);
    m_buffer[1] = static_cast<std::uint8_t>(opcode >> 8u);

    if (payload != nullptr && payload_bytes != 0)
        std::memcpy(m_buffer.data() + OpcodeSize, payload, payload_bytes);
}

bool ApplicationPacket::Decode(const void *data, std::uint32_t bytes)
{
    if (data == nullptr || bytes < OpcodeSize)
        return false;

    const auto *packet = static_cast<const std::uint8_t *>(data);
    m_opcode = static_cast<std::uint16_t>(packet[0]) | static_cast<std::uint16_t>(packet[1] << 8u);
    m_buffer.assign(packet, packet + bytes);
    return true;
}

std::uint16_t ApplicationPacket::Opcode() const
{
    return m_opcode;
}

const std::uint8_t *ApplicationPacket::Payload() const
{
    return m_buffer.size() > OpcodeSize ? m_buffer.data() + OpcodeSize : nullptr;
}

std::uint32_t ApplicationPacket::PayloadSize() const
{
    return m_buffer.size() >= OpcodeSize ? static_cast<std::uint32_t>(m_buffer.size() - OpcodeSize) : 0;
}

const std::uint8_t *ApplicationPacket::Data() const
{
    return m_buffer.empty() ? nullptr : m_buffer.data();
}

std::uint32_t ApplicationPacket::Size() const
{
    return static_cast<std::uint32_t>(m_buffer.size());
}
