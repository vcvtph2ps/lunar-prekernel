#pragma once
#include <stddef.h>
#include <stdint.h>

// NOLINTBEGIN
void memset(void* dest, int ch, size_t count);
void memcpy(void* dest, const void* src, size_t count);
void memmove(void* dest, const void* src, size_t count);
[[nodiscard]] int memcmp(const void* lhs, const void* rhs, size_t count);
[[nodiscard]] int strcmp(const char* s1, const char* s2);
[[nodiscard]] int strlen(const char* str);
[[nodiscard]] size_t strnlen(const char* s, size_t maxlen);
[[nodiscard]] char* strrchr(const char* s, int c);
[[nodiscard]] void* memchr(const void* s, int c, size_t n);
[[nodiscard]] const char* strstr(const char* haystack, const char* needle);

typedef uintptr_t phys_addr_t;
typedef uintptr_t virt_addr_t;
// NOLINTEND
