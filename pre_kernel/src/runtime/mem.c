#include <runtime/mem.h>
#include <stdint.h>

[[gnu::weak]] void memset(void* dest, int ch, size_t count) {
    for(size_t i = 0; i < count; i++) ((uint8_t*) dest)[i] = (unsigned char) ch;
}

[[gnu::weak]] void memcpy(void* dest, const void* src, size_t count) {
    for(size_t i = 0; i < count; i++) ((uint8_t*) dest)[i] = ((uint8_t*) src)[i];
}

[[gnu::weak]] void memmove(void* dest, const void* src, size_t count) {
    if(src == dest) return;
    if(src > dest) {
        for(size_t i = 0; i < count; i++) ((uint8_t*) dest)[i] = ((uint8_t*) src)[i];
    } else {
        for(size_t i = count; i > 0; i--) ((uint8_t*) dest)[i - 1] = ((uint8_t*) src)[i - 1];
    }
}

int memcmp(const void* lhs, const void* rhs, size_t count) {
    for(size_t i = 0; i < count; i++) {
        if(*((uint8_t*) lhs + i) > *((uint8_t*) rhs + i)) return -1;
        if(*((uint8_t*) lhs + i) < *((uint8_t*) rhs + i)) return 1;
    }
    return 0;
}

int strlen(const char* str) {
    int length = 0;
    while(str[length] != '\0') length++;
    return length;
}

size_t strnlen(const char* s, size_t maxlen) {
    size_t len;
    for(len = 0; len < maxlen; len++) {
        if(s[len] == '\0') break;
    }
    return len;
}

char* strrchr(const char* s, int c) {
    char* last = nullptr;
    do {
        if(*s == (char) c) last = (char*) s;
    } while(*s++);
    return last;
}

void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*) s;
    for(size_t i = 0; i < n; i++) {
        if(p[i] == (unsigned char) c) return (void*) (p + i);
    }
    return nullptr;
}

int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*) s1 - *(const unsigned char*) s2;
}

const char* strstr(const char* haystack, const char* needle) {
    if(!*needle) return haystack;
    for(; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while(*h && *n && *h == *n) { h++; n++; }
        if(!*n) return haystack;
    }
    return nullptr;
}
