#pragma once

#include "core.h"

#include <cstddef>

class Arena
{
public:
    Arena(u64 reserve_size, u64 initial_commit = 0);
    ~Arena();

    template<typename T>
    T* push(u64 count = 1);

    void reset();

private:
    u64 get_page_size();
    u64 align_up(u64 value, u64 alignment);
    bool commit(u64 required);
    void* push_size(u64 size, u64 alignment = alignof(std::max_align_t));

private:
    u8 *base = nullptr;
    u64 reserved = 0;
    u64 committed = 0;
    u64 used = 0;
    u64 page_size = 0;
};
