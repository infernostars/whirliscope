#include "stdio.h"

#include "string.h"
#include <stdbool.h>
#include <stdint.h>

#define PRINT_BUFFER_SIZE 512

struct out {
    void (*put)(char ch, void *ctx);
    void *ctx;
    size_t count;
};

struct buffer_out {
    char *buffer;
    size_t size;
    size_t *count;
};

struct stream_out {
    char buffer[PRINT_BUFFER_SIZE];
    size_t len;
};

enum int_len {
    INT_LEN_DEFAULT,
    INT_LEN_LONG,
    INT_LEN_LONG_LONG,
    INT_LEN_SIZE,
};

static void emit_char(struct out *out, char ch) {
    out->put(ch, out->ctx);
    out->count++;
}

static void emit_repeat(struct out *out, char ch, int count) {
    for (int i = 0; i < count; i++) {
        emit_char(out, ch);
    }
}

static int decimal_digit(char ch) {
    if (ch < '0' || ch > '9') {
        return -1;
    }

    return ch - '0';
}

static unsigned long long read_unsigned(va_list args, enum int_len len) {
    switch (len) {
        case INT_LEN_LONG:
            return va_arg(args, unsigned long);
        case INT_LEN_LONG_LONG:
            return va_arg(args, unsigned long long);
        case INT_LEN_SIZE:
            return va_arg(args, size_t);
        case INT_LEN_DEFAULT:
        default:
            return va_arg(args, unsigned int);
    }
}

static long long read_signed(va_list args, enum int_len len) {
    switch (len) {
        case INT_LEN_LONG:
            return va_arg(args, long);
        case INT_LEN_LONG_LONG:
            return va_arg(args, long long);
        case INT_LEN_SIZE:
            return (long long)va_arg(args, size_t);
        case INT_LEN_DEFAULT:
        default:
            return va_arg(args, int);
    }
}

static int format_unsigned(char *buffer, unsigned long long value,
                           unsigned int base, bool uppercase) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int len = 0;

    if (value == 0) {
        buffer[len++] = '0';
        return len;
    }

    while (value != 0) {
        buffer[len++] = digits[value % base];
        value /= base;
    }

    return len;
}

static void emit_padded_string(struct out *out, const char *s,
                               int len, int width, bool left_adjust) {
    int padding = width > len ? width - len : 0;

    if (!left_adjust) {
        emit_repeat(out, ' ', padding);
    }

    for (int i = 0; i < len; i++) {
        emit_char(out, s[i]);
    }

    if (left_adjust) {
        emit_repeat(out, ' ', padding);
    }
}

static void emit_number(struct out *out, unsigned long long value,
                        bool negative, unsigned int base, bool uppercase,
                        int width, int precision, bool left_adjust,
                        bool zero_pad, const char *prefix, int prefix_len) {
    char digits[64];
    int digit_len = format_unsigned(digits, value, base, uppercase);

    if (precision == 0 && value == 0) {
        digit_len = 0;
    }

    int sign_len = negative ? 1 : 0;
    int zeroes = precision > digit_len ? precision - digit_len : 0;
    int total_len = sign_len + prefix_len + zeroes + digit_len;
    int spaces = width > total_len ? width - total_len : 0;

    if (!left_adjust && !zero_pad) {
        emit_repeat(out, ' ', spaces);
    }

    if (negative) {
        emit_char(out, '-');
    }

    for (int i = 0; i < prefix_len; i++) {
        emit_char(out, prefix[i]);
    }

    if (!left_adjust && zero_pad && precision < 0) {
        emit_repeat(out, '0', spaces);
    }

    emit_repeat(out, '0', zeroes);

    for (int i = digit_len - 1; i >= 0; i--) {
        emit_char(out, digits[i]);
    }

    if (left_adjust) {
        emit_repeat(out, ' ', spaces);
    }
}

static void buffer_put(char ch, void *ctx) {
    struct buffer_out *buffer_out = ctx;

    if (buffer_out->size > 0 && *buffer_out->count + 1 < buffer_out->size) {
        buffer_out->buffer[*buffer_out->count] = ch;
    }
}

static void stream_flush(struct stream_out *stream) {
    if (stream->len > 0) {
        whrlibc_write(stream->buffer, stream->len);
        stream->len = 0;
    }
}

static void stream_put(char ch, void *ctx) {
    struct stream_out *stream = ctx;

    if (stream->len == sizeof(stream->buffer)) {
        stream_flush(stream);
    }

    stream->buffer[stream->len++] = ch;
}

static int vformat(struct out *out, const char *fmt, va_list args) {
    while (*fmt != '\0') {
        if (*fmt != '%') {
            emit_char(out, *fmt++);
            continue;
        }

        fmt++;

        if (*fmt == '%') {
            emit_char(out, *fmt++);
            continue;
        }

        bool left_adjust = false;
        bool zero_pad = false;
        bool alternate_form = false;

        bool parsing_flags = true;
        while (parsing_flags) {
            switch (*fmt) {
                case '-':
                    left_adjust = true;
                    fmt++;
                    break;
                case '0':
                    zero_pad = true;
                    fmt++;
                    break;
                case '#':
                    alternate_form = true;
                    fmt++;
                    break;
                default:
                    parsing_flags = false;
                    break;
            }
        }

        int width = 0;
        if (*fmt == '*') {
            width = va_arg(args, int);
            if (width < 0) {
                left_adjust = true;
                width = -width;
            }
            fmt++;
        } else {
            int digit;
            while ((digit = decimal_digit(*fmt)) >= 0) {
                width = width * 10 + digit;
                fmt++;
            }
        }

        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt == '*') {
                precision = va_arg(args, int);
                fmt++;
            } else {
                int digit;
                while ((digit = decimal_digit(*fmt)) >= 0) {
                    precision = precision * 10 + digit;
                    fmt++;
                }
            }
        }

        enum int_len len = INT_LEN_DEFAULT;
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                len = INT_LEN_LONG_LONG;
                fmt++;
            } else {
                len = INT_LEN_LONG;
            }
        } else if (*fmt == 'z') {
            len = INT_LEN_SIZE;
            fmt++;
        }

        char spec = *fmt;
        if (spec == '\0') {
            break;
        }
        fmt++;

        switch (spec) {
            case 'c': {
                char ch = (char)va_arg(args, int);
                emit_padded_string(out, &ch, 1, width, left_adjust);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                if (s == NULL) {
                    s = "(null)";
                }

                int str_len = 0;
                while (s[str_len] != '\0'
                    && (precision < 0 || str_len < precision)) {
                    str_len++;
                }

                emit_padded_string(out, s, str_len, width, left_adjust);
                break;
            }
            case 'd':
            case 'i': {
                long long signed_value = read_signed(args, len);
                bool negative = signed_value < 0;
                unsigned long long value = negative
                    ? 0 - (unsigned long long)signed_value
                    : (unsigned long long)signed_value;
                emit_number(out, value, negative, 10, false, width, precision,
                            left_adjust, zero_pad, NULL, 0);
                break;
            }
            case 'u':
                emit_number(out, read_unsigned(args, len), false, 10, false,
                            width, precision, left_adjust, zero_pad, NULL, 0);
                break;
            case 'b': {
                unsigned long long value = read_unsigned(args, len);
                const char *prefix = alternate_form && value != 0 ? "0b" : NULL;
                int prefix_len = prefix != NULL ? 2 : 0;
                emit_number(out, value, false, 2, false,
                            width, precision, left_adjust, zero_pad,
                            prefix, prefix_len);
                break;
            }
            case 'B': {
                unsigned long long value = read_unsigned(args, len);
                const char *prefix = alternate_form && value != 0 ? "0B" : NULL;
                int prefix_len = prefix != NULL ? 2 : 0;
                emit_number(out, value, false, 2, true,
                            width, precision, left_adjust, zero_pad,
                            prefix, prefix_len);
                break;
            }
            case 'x': {
                unsigned long long value = read_unsigned(args, len);
                const char *prefix = alternate_form && value != 0 ? "0x" : NULL;
                int prefix_len = prefix != NULL ? 2 : 0;
                emit_number(out, value, false, 16, false,
                            width, precision, left_adjust, zero_pad,
                            prefix, prefix_len);
                break;
            }
            case 'X': {
                unsigned long long value = read_unsigned(args, len);
                const char *prefix = alternate_form && value != 0 ? "0X" : NULL;
                int prefix_len = prefix != NULL ? 2 : 0;
                emit_number(out, value, false, 16, true,
                            width, precision, left_adjust, zero_pad,
                            prefix, prefix_len);
                break;
            }
            case 'o': {
                unsigned long long value = read_unsigned(args, len);
                const char *prefix = alternate_form && value != 0 ? "0" : NULL;
                int prefix_len = prefix != NULL ? 1 : 0;
                emit_number(out, value, false, 8, false,
                            width, precision, left_adjust, zero_pad,
                            prefix, prefix_len);
                break;
            }
            case 'p': {
                uintptr_t ptr = (uintptr_t)va_arg(args, void *);
                emit_number(out, ptr, false, 16, false,
                            width, precision, left_adjust, zero_pad, "0x", 2);
                break;
            }
            default:
                emit_char(out, '%');
                emit_char(out, spec);
                break;
        }
    }

    return (int)out->count;
}

int putchar(int ch) {
    char out = (char)ch;
    whrlibc_write(&out, 1);
    return (unsigned char)ch;
}

int fputs(const char *s) {
    whrlibc_write(s, strlen(s));
    return 0;
}

int puts(const char *s) {
    fputs(s);
    whrlibc_write("\n", 1);
    return 0;
}

int printf(const char *restrict fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vprintf(fmt, args);
    va_end(args);
    return written;
}

int vprintf(const char *restrict fmt, va_list args) {
    struct stream_out stream = {
        .len = 0,
    };
    struct out out = {
        .put = stream_put,
        .ctx = &stream,
        .count = 0,
    };

    int written = vformat(&out, fmt, args);
    stream_flush(&stream);
    return written;
}

int snprintf(char *restrict s, size_t n, const char *restrict fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(s, n, fmt, args);
    va_end(args);
    return written;
}

int vsnprintf(char *restrict s, size_t n, const char *restrict fmt,
              va_list args) {
    struct out out = {
        .put = buffer_put,
        .ctx = NULL,
        .count = 0,
    };
    struct buffer_out buffer_out = {
        .buffer = s,
        .size = n,
        .count = &out.count,
    };

    out.ctx = &buffer_out;

    int written = vformat(&out, fmt, args);

    if (n > 0) {
        size_t terminator = out.count < n ? out.count : n - 1;
        s[terminator] = '\0';
    }

    return written;
}
