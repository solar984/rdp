// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#ifndef RDP_EXAMPLE_RUNTIME_H
#define RDP_EXAMPLE_RUNTIME_H

#include <cstdint>

#include "rdplib.h"

class RDPEndpoint;

// Owns the process rdplib runtime.
class RDPRuntime
{
public:
    RDPRuntime();
    ~RDPRuntime();

    RDPRuntime(const RDPRuntime &) = delete;
    RDPRuntime &operator=(const RDPRuntime &) = delete;

    // This is an initial allocator size, not a memory limit.
    int Open(std::uint32_t fast_allocator_bytes = 4u * 1024u * 1024u);

    // Every endpoint must be closed first.
    int Close();

    bool IsOpen() const
    {
        return m_runtime != nullptr;
    }

private:
    friend class RDPEndpoint;

    rdplib_runtime_t *m_runtime;
};

#endif // RDP_EXAMPLE_RUNTIME_H
