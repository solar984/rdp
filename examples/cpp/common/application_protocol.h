// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDP_EXAMPLE_APPLICATION_PROTOCOL_H
#define RDP_EXAMPLE_APPLICATION_PROTOCOL_H

#include <cstdint>

enum class ApplicationOpcode : std::uint16_t
{
    LoginRequest = 1,
    LoginReply = 2,
    ClientReady = 3,
    ServerReady = 4,
    ClientPositionUpdate = 5,
    SaveProfile = 6,
    LogoutRequest = 7,
    LogoutReply = 8
};

enum class LogoutReason : std::uint32_t
{
    ExampleComplete = 1
};

static constexpr std::uint32_t ApplicationProtocolVersion = 1;

// These fixed payload layouts use the little endian integer and IEEE float
// representation used by the example's supported Windows and Linux targets.
#pragma pack(push, 1)
struct LoginRequest
{
    std::uint32_t protocol_version;
};

struct LoginReply
{
    std::uint32_t accepted;
};

struct ClientPositionUpdate
{
    std::uint32_t sequence;
    float x;
    float y;
    float z;
    float heading;
};

struct SaveProfile
{
    std::uint32_t sequence;
    std::uint32_t seconds_in_game;
    float x;
    float y;
    float z;
    float heading;
};

struct LogoutRequest
{
    std::uint32_t reason;
};

struct LogoutReply
{
    std::uint32_t reason;
};
#pragma pack(pop)

static_assert(sizeof(LoginRequest) == 4, "LoginRequest wire size changed");
static_assert(sizeof(LoginReply) == 4, "LoginReply wire size changed");
static_assert(sizeof(ClientPositionUpdate) == 20, "ClientPositionUpdate wire size changed");
static_assert(sizeof(SaveProfile) == 24, "SaveProfile wire size changed");
static_assert(sizeof(LogoutRequest) == 4, "LogoutRequest wire size changed");
static_assert(sizeof(LogoutReply) == 4, "LogoutReply wire size changed");

#endif // RDP_EXAMPLE_APPLICATION_PROTOCOL_H
