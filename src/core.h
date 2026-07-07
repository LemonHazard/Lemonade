#pragma once

#include <cstdint>

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float  r32;
typedef double r64;

typedef const char *cstr;

constexpr u64 KB = 1000;
constexpr u64 MB = 1000 * KB;
constexpr u64 GB = 1000 * MB;

constexpr u64 KiB = 1024;
constexpr u64 MiB = 1024 * KiB;
constexpr u64 GiB = 1024 * MiB;
