#include "string.h"

size_t strlen(const char *s) {
    size_t len = 0;

    while (s[len] != '\0') {
        len++;
    }

    return len;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t len = 0;

    while (len < maxlen && s[len] != '\0') {
        len++;
    }

    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        s1++;
        s2++;
    }

    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];

        if (c1 != c2 || c1 == '\0') {
            return c1 - c2;
        }
    }

    return 0;
}

char *strcpy(char *restrict dest, const char *restrict src) {
    char *ret = dest;

    while ((*dest++ = *src++) != '\0') {
    }

    return ret;
}

char *strncpy(char *restrict dest, const char *restrict src, size_t n) {
    size_t i = 0;

    for (; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }

    for (; i < n; i++) {
        dest[i] = '\0';
    }

    return dest;
}

char *strchr(const char *s, int c) {
    char ch = (char)c;

    for (;; s++) {
        if (*s == ch) {
            return (char *)s;
        }

        if (*s == '\0') {
            return NULL;
        }
    }
}

char *strrchr(const char *s, int c) {
    char ch = (char)c;
    const char *last = NULL;

    for (;; s++) {
        if (*s == ch) {
            last = s;
        }
        if (*s == '\0') {
            return (char *)last;
        }
    }
}
