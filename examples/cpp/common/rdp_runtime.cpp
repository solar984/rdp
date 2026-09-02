// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "rdp_runtime.h"

#include <cassert>

RDPRuntime::RDPRuntime()
    : m_runtime(nullptr)
{
}

RDPRuntime::~RDPRuntime()
{
    int result = Close();
    assert(result == RDPLIB_OK);
    (void)result;
}

int RDPRuntime::Open(std::uint32_t fast_allocator_bytes)
{
    if (m_runtime != nullptr)
        return RDPLIB_ERROR_BUSY;

    return rdplib_runtime_create(&m_runtime, fast_allocator_bytes);
}

int RDPRuntime::Close()
{
    if (m_runtime == nullptr)
        return RDPLIB_OK;

    int result = rdplib_runtime_destroy(m_runtime);
    if (result == RDPLIB_OK)
        m_runtime = nullptr;

    return result;
}
