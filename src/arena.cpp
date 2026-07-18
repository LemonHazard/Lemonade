#include "arena.h"

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
    this->page_size = this->get_page_size();

    reserve_size   = this->align_up(reserve_size, this->page_size);
    initial_commit = this->align_up(initial_commit, this->page_size);

    if (initial_commit > reserve_size)
    {
        // return false;
    }

#ifdef OS_WINDOWS
    this->base = (u8*)VirtualAlloc(nullptr,
                                   reserve_size,
                                   MEM_RESERVE,
                                   PAGE_READWRITE);

    if (!this->base)
    {
        // return false;
    }

    if (initial_commit)
    {
        void* result = VirtualAlloc(this->base,
                                    initial_commit,
                                    MEM_COMMIT,
                                    PAGE_READWRITE);

        if (!result)
        {
            VirtualFree(this->base, 0, MEM_RELEASE);
            // return false;
        }
    }
#else
    this->base = (u8*)mmap(nullptr,
                           reserve_size,
                           PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS,
                           -1,
                           0);

    if (this->base == MAP_FAILED)
//        return false;

    if (initial_commit)
    {
        if (mprotect(this->base,
                     initial_commit,
                     PROT_READ | PROT_WRITE) != 0)
        {
            munmap(this->base, reserve_size);
//            return false;
        }
    }
#endif

    this->reserved  = reserve_size;
    this->committed = initial_commit;
    this->used      = 0;

    // return true;
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
