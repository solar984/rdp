// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

using System;
using System.Buffers.Binary;

namespace RdplibExample.Common
{
    internal enum ApplicationOpcode : ushort
    {
        LoginRequest = 1,
        LoginReply = 2,
        ClientReady = 3,
        ServerReady = 4,
        ClientPositionUpdate = 5,
        SaveProfile = 6,
        LogoutRequest = 7,
        LogoutReply = 8
    }

    internal enum LogoutReason : uint
    {
        ExampleComplete = 1
    }

    internal struct LoginRequest
    {
        internal uint ProtocolVersion;
    }

    internal struct LoginReply
    {
        internal uint Accepted;
    }

    internal struct ClientPositionUpdate
    {
        internal uint Sequence;
        internal float X;
        internal float Y;
        internal float Z;
        internal float Heading;
    }

    internal struct SaveProfile
    {
        internal uint Sequence;
        internal uint SecondsInGame;
        internal float X;
        internal float Y;
        internal float Z;
        internal float Heading;
    }

    internal struct LogoutRequest
    {
        internal uint Reason;
    }

    internal struct LogoutReply
    {
        internal uint Reason;
    }

    // These fixed payload layouts use explicit little endian encoding. The
    // floating point fields use the IEEE single precision representation used
    // by the example's supported Windows and Linux targets.
    internal static class ApplicationProtocol
    {
        internal const uint Version = 1;

        internal static byte[] Encode(LoginRequest value)
        {
            byte[] payload = new byte[4];
            WriteUInt32(payload, 0, value.ProtocolVersion);
            return payload;
        }

        internal static byte[] Encode(LoginReply value)
        {
            byte[] payload = new byte[4];
            WriteUInt32(payload, 0, value.Accepted);
            return payload;
        }

        internal static byte[] Encode(ClientPositionUpdate value)
        {
            byte[] payload = new byte[20];
            WriteUInt32(payload, 0, value.Sequence);
            WriteSingle(payload, 4, value.X);
            WriteSingle(payload, 8, value.Y);
            WriteSingle(payload, 12, value.Z);
            WriteSingle(payload, 16, value.Heading);
            return payload;
        }

        internal static byte[] Encode(SaveProfile value)
        {
            byte[] payload = new byte[24];
            WriteUInt32(payload, 0, value.Sequence);
            WriteUInt32(payload, 4, value.SecondsInGame);
            WriteSingle(payload, 8, value.X);
            WriteSingle(payload, 12, value.Y);
            WriteSingle(payload, 16, value.Z);
            WriteSingle(payload, 20, value.Heading);
            return payload;
        }

        internal static byte[] Encode(LogoutRequest value)
        {
            byte[] payload = new byte[4];
            WriteUInt32(payload, 0, value.Reason);
            return payload;
        }

        internal static byte[] Encode(LogoutReply value)
        {
            byte[] payload = new byte[4];
            WriteUInt32(payload, 0, value.Reason);
            return payload;
        }

        internal static bool TryDecode(byte[] payload, out LoginRequest value)
        {
            value = new LoginRequest();
            if (payload == null || payload.Length != 4)
                return false;

            value.ProtocolVersion = ReadUInt32(payload, 0);
            return true;
        }

        internal static bool TryDecode(byte[] payload, out LoginReply value)
        {
            value = new LoginReply();
            if (payload == null || payload.Length != 4)
                return false;

            value.Accepted = ReadUInt32(payload, 0);
            return true;
        }

        internal static bool TryDecode(byte[] payload, out ClientPositionUpdate value)
        {
            value = new ClientPositionUpdate();
            if (payload == null || payload.Length != 20)
                return false;

            value.Sequence = ReadUInt32(payload, 0);
            value.X = ReadSingle(payload, 4);
            value.Y = ReadSingle(payload, 8);
            value.Z = ReadSingle(payload, 12);
            value.Heading = ReadSingle(payload, 16);
            return true;
        }

        internal static bool TryDecode(byte[] payload, out SaveProfile value)
        {
            value = new SaveProfile();
            if (payload == null || payload.Length != 24)
                return false;

            value.Sequence = ReadUInt32(payload, 0);
            value.SecondsInGame = ReadUInt32(payload, 4);
            value.X = ReadSingle(payload, 8);
            value.Y = ReadSingle(payload, 12);
            value.Z = ReadSingle(payload, 16);
            value.Heading = ReadSingle(payload, 20);
            return true;
        }

        internal static bool TryDecode(byte[] payload, out LogoutRequest value)
        {
            value = new LogoutRequest();
            if (payload == null || payload.Length != 4)
                return false;

            value.Reason = ReadUInt32(payload, 0);
            return true;
        }

        internal static bool TryDecode(byte[] payload, out LogoutReply value)
        {
            value = new LogoutReply();
            if (payload == null || payload.Length != 4)
                return false;

            value.Reason = ReadUInt32(payload, 0);
            return true;
        }

        private static void WriteUInt32(byte[] payload, int offset, uint value)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(offset, 4), value);
        }

        private static uint ReadUInt32(byte[] payload, int offset)
        {
            return BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(offset, 4));
        }

        private static void WriteSingle(byte[] payload, int offset, float value)
        {
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(offset, 4), value);
        }

        private static float ReadSingle(byte[] payload, int offset)
        {
            return BinaryPrimitives.ReadSingleLittleEndian(payload.AsSpan(offset, 4));
        }
    }
}
