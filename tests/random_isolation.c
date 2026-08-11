// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdp.h"

int main(void)
{
    enum
    {
        TEST_RANDOM_SEED = 12345
    };
    rdp_t owner;
    connection_t connection;
    uint8_t remote_address[16] = {0};
    uint16_t address_family = RDP_TRANSMIT_ADDRESS_IPV4;
    int expected_first;
    int expected_second;
    int observed_first;
    int observed_second;

    srand(TEST_RANDOM_SEED);
    expected_first = rand();
    expected_second = rand();

    srand(TEST_RANDOM_SEED);
    observed_first = rand();
    memset(&owner, 0, sizeof(owner));
    memset(&connection, 0, sizeof(connection));
    memcpy(remote_address, &address_family, sizeof(address_family));
    owner.ipv4_socket = -1;
    owner.icmp_probe_socket = -1;
    tx_init(&connection, &owner, remote_address);
    observed_second = rand();

    printf("first_preserved=%d second_preserved=%d initial_message_id=%u\n", observed_first == expected_first, observed_second == expected_second, connection.transmit.initial_outgoing_message_id);
    return observed_first == expected_first && observed_second == expected_second ? 0 : 1;
}
