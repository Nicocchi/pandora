#include "string.h"

extern "C" {
void *memcpy(void *__restrict dest, const void *__restrict src, size_t n) {
    uint8_t *__restrict pdest = (uint8_t*)dest;
    const uint8_t *__restrict psrc = (uint8_t*)src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t*)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t*)dest;
    const uint8_t *psrc = (uint8_t*)src;

    if ((uintptr_t)src > (uintptr_t)dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if ((uintptr_t)src < (uintptr_t)dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (uint8_t*)s1;
    const uint8_t *p2 = (uint8_t*)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

int strlen(const char s[])
{
    int i = 0;
    while (s[i] != '\0')
    {
        ++i;
    }
    return i;
}

bool CheckStringEndsWith(const char *str, const char *end)
{
    // Return early if either pointer is null to avoid page fault dereferencing
    if (!str || !end) return false;

    const char *_str = str;
    const char *_end = end;

    // Handle trivial matching scenario where suffix query string is blank
    if (*end == '\0') return true;
    if (*str == '\0') return false;

    // Wind the main tracking pointer to end of the input string
    while (*str != 0) str++;
    str--;

    // Wind the comparison tracking pointer to the end of the target suffix string
    while (*end != 0) end++;
    end--;

    while (true)
    {
        if (*str != *end) return false;

        str--;
        end--;

        if (end < _end) return true;
        if (str < _str && end >= _end) return false;
    }

    return true;
}

} // extern "C"
