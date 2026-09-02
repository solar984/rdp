// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

using System;

namespace RdplibExample.Common
{
    // The application protocol uses a 2 byte little endian opcode followed by
    // an opcode-specific payload. RDP treats the complete buffer as one message.
    internal sealed class ApplicationPacket
    {
        private const int OpcodeSize = 2;

        internal ApplicationOpcode Opcode { get; private set; }
        internal byte[] Payload { get; private set; }
        internal byte[] Data { get; private set; }

        internal ApplicationPacket(ApplicationOpcode opcode, byte[] payload = null)
        {
            Opcode = opcode;
            Payload = payload ?? Array.Empty<byte>();
            Data = new byte[OpcodeSize + Payload.Length];
            Data[0] = (byte)((ushort)opcode & 0xff);
            Data[1] = (byte)((ushort)opcode >> 8);
            if (Payload.Length != 0)
                Buffer.BlockCopy(Payload, 0, Data, OpcodeSize, Payload.Length);
        }

        private ApplicationPacket()
        {
            Payload = Array.Empty<byte>();
            Data = Array.Empty<byte>();
        }

        internal static bool TryDecode(byte[] data, out ApplicationPacket packet)
        {
            packet = null;
            if (data == null || data.Length < OpcodeSize)
                return false;

            ApplicationPacket decoded = new ApplicationPacket();
            decoded.Opcode = (ApplicationOpcode)(data[0] | (data[1] << 8));
            decoded.Data = data;
            decoded.Payload = new byte[data.Length - OpcodeSize];
            if (decoded.Payload.Length != 0)
                Buffer.BlockCopy(data, OpcodeSize, decoded.Payload, 0, decoded.Payload.Length);

            packet = decoded;
            return true;
        }
    }
}
