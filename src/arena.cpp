#include "arena.h"
#include "logger.h"

#include <cassert>

#ifdef OS_WINDOWS
    #define NOMINMAX
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

Arena::Arena(u64 reserve_size, u64 initial_commit)
{
    // A constructor can't report failure, so on any error the arena is left
    // empty (base == nullptr): is_valid() returns false and push() returns nullptr.

    this->page_size = this->get_page_size();

    reserve_size   = this->align_up(reserve_size, this->page_size);
    initial_commit = this->align_up(initial_commit, this->page_size);

    if (reserve_size == 0 || initial_commit > reserve_size)
    {
        log_error("Arena: invalid sizes (reserve %llu, initial commit %llu)",
                  (unsigned long long)reserve_size,
                  (unsigned long long)initial_commit);
        return;
    }

#ifdef OS_WINDOWS
    u8 *memory = (u8*)VirtualAlloc(nullptr,
                                   reserve_size,
                                   MEM_RESERVE,
                                   PAGE_READWRITE);

    if (!memory)
    {
        log_error("Arena: failed to reserve %llu bytes", (unsigned long long)reserve_size);
        return;
    }

    if (initial_commit)
    {
        void* result = VirtualAlloc(memory,
                                    initial_commit,
                                    MEM_COMMIT,
                                    PAGE_READWRITE);

        if (!result)
        {
            log_error("Arena: failed to commit %llu bytes", (unsigned long long)initial_commit);
            VirtualFree(memory, 0, MEM_RELEASE);
            return;
        }
    }
#else
    void *mapping = mmap(nullptr,
                         reserve_size,
                         PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         -1,
                         0);

    if (mapping == MAP_FAILED)
    {
        log_error("Arena: failed to reserve %llu bytes", (unsigned long long)reserve_size);
        return;
    }

    u8 *memory = (u8*)mapping;

    if (initial_commit)
    {
        if (mprotect(memory,
                     initial_commit,
                     PROT_READ | PROT_WRITE) != 0)
        {
            log_error("Arena: failed to commit %llu bytes", (unsigned long long)initial_commit);
            munmap(memory, reserve_size);
            return;
        }
    }
#endif

    this->base      = memory;
    this->reserved  = reserve_size;
    this->committed = initial_commit;
    this->used      = 0;
}

bool Arena::is_valid() const
{
    return this->base != nullptr;
}

Arena::~Arena()
{
#ifdef OS_WINDOWS
    if (this->base) VirtualFree(this->base, 0, MEM_RELEASE);
#else
    if (this->base) munmap(this->base, this->reserved);
#endif

    this->base      = nullptr;
    this->reserved  = 0;
    this->committed = 0;
    this->used      = 0;
    this->page_size = 0;
}

template<typename T>
T* Arena::push(u64 count)
{
    return (T*)this->push_size(sizeof(T) * count, alignof(T));
}

void* Arena::push_size(u64 size, u64 alignment)
{
    u64 pos = this->align_up(this->used, alignment);
    u64 end = pos + size;

    if (!this->commit(end))
        return nullptr;

    void* result = this->base + pos;
    this->used = end;

    return result;
}

u64 Arena::align_up(u64 value, u64 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

bool Arena::commit(u64 required)
{
    if (!this->base)
        return false;

    if (required <= this->committed)
        return true;

    u64 new_commit = this->align_up(required, this->page_size);

    if (new_commit > this->reserved)
        return false;

#ifdef OS_WINDOWS
    void* result = VirtualAlloc(this->base + this->committed,
                                new_commit - this->committed,
                                MEM_COMMIT,
                                PAGE_READWRITE);

    if (!result)
        return false;
#else
    if (mprotect(this->base + this->committed,
                 new_commit - this->committed,
                 PROT_READ | PROT_WRITE) != 0)
    {
        return false;
    }
#endif

    this->committed = new_commit;

    return true;
}

u64 Arena::get_page_size()
{
#ifdef OS_WINDOWS
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (u64)info.dwPageSize;
#else
    return (u64)sysconf(_SC_PAGESIZE);
#endif
}

void Arena::reset()
{
    this->used = 0;
}
