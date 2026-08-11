// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;
using RdplibExample.Common;

namespace RdplibExample.Client
{
    internal sealed class GameManager
    {
        private static readonly TimeSpan PositionUpdateInterval = TimeSpan.FromSeconds(1);
        private static readonly TimeSpan ProfileSaveInterval = TimeSpan.FromSeconds(6);
        private static readonly TimeSpan GameDuration = TimeSpan.FromSeconds(30);

        private readonly Stopwatch _gameTimer;
        private bool _started;
        private TimeSpan _nextPositionUpdate;
        private TimeSpan _nextProfileSave;
        private uint _positionSequence;
        private uint _saveSequence;
        private float _x;
        private float _y;
        private float _z;
        private float _heading;

        internal GameManager()
        {
            _gameTimer = new Stopwatch();
            _z = 0.0f;
        }

        internal bool ProcessGame(ServerManager server)
        {
            if (!_started)
            {
                _started = true;
                _nextPositionUpdate = TimeSpan.Zero;
                _nextProfileSave = ProfileSaveInterval;
                _gameTimer.Start();
                Console.WriteLine("game simulation started");
            }

            TimeSpan now = _gameTimer.Elapsed;
            if (now >= GameDuration)
            {
                if (!server.SendLogout(LogoutReason.ExampleComplete))
                    return false;

                Console.WriteLine("requested logout with reason {0}", (uint)LogoutReason.ExampleComplete);
                return true;
            }

            // ClientPositionUpdate is an example unreliable gameplay message.
            if (now >= _nextPositionUpdate)
            {
                ++_positionSequence;
                _x += 1.0f;
                _y += 0.5f;
                _heading += 8.0f;

                ClientPositionUpdate update = new ClientPositionUpdate();
                update.Sequence = _positionSequence;
                update.X = _x;
                update.Y = _y;
                update.Z = _z;
                update.Heading = _heading;

                if (!server.SendClientPositionUpdate(update))
                    return false;

                Console.WriteLine("sent unreliable position update {0}", update.Sequence);
                _nextPositionUpdate = now + PositionUpdateInterval;
            }

            // SaveProfile is an example reliable gameplay message.
            if (now >= _nextProfileSave)
            {
                SaveProfile profile = new SaveProfile();
                profile.Sequence = ++_saveSequence;
                profile.SecondsInGame = (uint)now.TotalSeconds;
                profile.X = _x;
                profile.Y = _y;
                profile.Z = _z;
                profile.Heading = _heading;

                if (!server.SendSaveProfile(profile))
                    return false;

                Console.WriteLine("sent reliable profile save {0}", profile.Sequence);
                _nextProfileSave = now + ProfileSaveInterval;
            }

            return true;
        }
    }
}
