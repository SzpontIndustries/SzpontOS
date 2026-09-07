#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <sys/syscall.h>

static FILE g_stdin_file = {.fd = STDIN_FILENO, .flags = 0, .error = 0, .eof = 0, .unget = 0, .has_unget = 0};
static FILE g_stdout_file = {.fd = STDOUT_FILENO, .flags = 0, .error = 0, .eof = 0, .unget = 0, .has_unget = 0};
static FILE g_stderr_file = {.fd = STDERR_FILENO, .flags = 0, .error = 0, .eof = 0, .unget = 0, .has_unget = 0};

FILE *stdin = &g_stdin_file;
FILE *stdout = &g_stdout_file;
FILE *stderr = &g_stderr_file;

FILE *fopen(const char *pathname, const char *mode) {
    if (!pathname || !mode)
        return NULL;
    int flags = 0;
    if (strchr(mode, '+')) {
        flags = O_RDWR;
        if (strchr(mode, 'w'))
            flags |= (O_CREAT | O_TRUNC);
        else if (strchr(mode, 'a'))
            flags |= (O_CREAT | O_APPEND);
    } else if (strchr(mode, 'w')) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (strchr(mode, 'a')) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else {
        flags = O_RDONLY;
    }

    int fd = open(pathname, flags, 0644);
    if (fd < 0)
        return NULL;

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        close(fd);
        return NULL;
    }
    memset(f, 0, sizeof(FILE));
    f->fd = fd;
    f->flags = flags;
    f->buf = (unsigned char *)malloc(BUFSIZ);
    if (f->buf) {
        f->buf_size = BUFSIZ;
        f->own_buf = 1;
        f->buf_mode = _IOFBF;
    } else {
        f->buf_mode = _IONBF;
    }
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    if (fd < 0)
        return NULL;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f)
        return NULL;
    memset(f, 0, sizeof(FILE));
    f->fd = fd;
    f->buf = (unsigned char *)malloc(BUFSIZ);
    if (f->buf) {
        f->buf_size = BUFSIZ;
        f->own_buf = 1;
        f->buf_mode = _IOFBF;
    } else {
        f->buf_mode = _IONBF;
    }
    return f;
}

FILE *freopen(const char *pathname, const char *mode, FILE *stream) {
    if (stream && stream->fd >= 0) {
        close(stream->fd);
    }
    FILE *new_f = fopen(pathname, mode);
    if (!new_f)
        return NULL;
    if (stream) {
        if (stream->own_buf && stream->buf) {
            free(stream->buf);
        }
        memcpy(stream, new_f, sizeof(FILE));
        free(new_f);
        return stream;
    }
    return new_f;
}

int fclose(FILE *stream) {
    if (!stream)
        return EOF;
    if (stream->fd >= 0) {
        close(stream->fd);
        stream->fd = -1;
    }
    if (stream->own_buf && stream->buf) {
        free(stream->buf);
        stream->buf = NULL;
    }
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0)
        return 0;
    size_t total_bytes = size * nmemb;
    size_t bytes_read = 0;
    uint8_t *dst = (uint8_t *)ptr;

    if (stream->has_unget) {
        dst[bytes_read++] = (uint8_t)stream->unget;
        stream->has_unget = 0;
    }

    /* 1. Consume existing buffered bytes */
    while (bytes_read < total_bytes && stream->buf && stream->buf_pos < stream->buf_end) {
        dst[bytes_read++] = stream->buf[stream->buf_pos++];
    }

    if (bytes_read >= total_bytes) {
        return bytes_read / size;
    }

    /* 2. Direct read if unbuffered or large chunk */
    if (stream->buf_mode == _IONBF || !stream->buf || (total_bytes - bytes_read) >= stream->buf_size) {
        while (bytes_read < total_bytes) {
            ssize_t ret = read(stream->fd, dst + bytes_read, total_bytes - bytes_read);
            if (ret < 0) {
                stream->error = 1;
                break;
            }
            if (ret == 0) {
                stream->eof = 1;
                break;
            }
            bytes_read += ret;
        }
    } else {
        /* 3. Refill buffer */
        ssize_t ret = read(stream->fd, stream->buf, stream->buf_size);
        if (ret > 0) {
            stream->buf_pos = 0;
            stream->buf_end = (size_t)ret;
            while (bytes_read < total_bytes && stream->buf_pos < stream->buf_end) {
                dst[bytes_read++] = stream->buf[stream->buf_pos++];
            }
        } else if (ret == 0) {
            stream->eof = 1;
        } else {
            stream->error = 1;
        }
    }

    return bytes_read / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0)
        return 0;
    size_t total_bytes = size * nmemb;
    size_t bytes_written = 0;
    const uint8_t *src = (const uint8_t *)ptr;

    while (bytes_written < total_bytes) {
        ssize_t ret = write(stream->fd, src + bytes_written, total_bytes - bytes_written);
        if (ret <= 0) {
            stream->error = 1;
            break;
        }
        bytes_written += ret;
    }
    return bytes_written / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream)
        return -1;
    stream->has_unget = 0;
    stream->eof = 0;
    stream->buf_pos = 0;
    stream->buf_end = 0;
    off_t ret = lseek(stream->fd, (off_t)offset, whence);
    return (ret == (off_t)-1) ? -1 : 0;
}

long ftell(FILE *stream) {
    if (!stream)
        return -1;
    long pos = (long)lseek(stream->fd, 0, SEEK_CUR);
    if (pos >= 0 && stream->buf && stream->buf_end > stream->buf_pos) {
        pos -= (long)(stream->buf_end - stream->buf_pos);
    }
    return pos;
}

int fseeko(FILE *stream, off_t offset, int whence) {
    return fseek(stream, (long)offset, whence);
}

off_t ftello(FILE *stream) {
    return (off_t)ftell(stream);
}

void rewind(FILE *stream) {
    if (stream)
        fseek(stream, 0, SEEK_SET);
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

int feof(FILE *stream) {
    return stream ? stream->eof : 1;
}

int ferror(FILE *stream) {
    return stream ? stream->error : 1;
}

void clearerr(FILE *stream) {
    if (stream) {
        stream->error = 0;
        stream->eof = 0;
    }
}

int fileno(FILE *stream) {
    return stream ? stream->fd : -1;
}

int fgetc(FILE *stream) {
    if (!stream)
        return EOF;
    if (stream->has_unget) {
        stream->has_unget = 0;
        return stream->unget;
    }
    if (stream->buf_mode == _IONBF || !stream->buf) {
        uint8_t c;
        ssize_t ret = read(stream->fd, &c, 1);
        if (ret <= 0) {
            if (ret == 0)
                stream->eof = 1;
            else
                stream->error = 1;
            return EOF;
        }
        return (int)c;
    }

    if (stream->buf_pos >= stream->buf_end) {
        ssize_t ret = read(stream->fd, stream->buf, stream->buf_size);
        if (ret <= 0) {
            if (ret == 0)
                stream->eof = 1;
            else
                stream->error = 1;
            return EOF;
        }
        stream->buf_pos = 0;
        stream->buf_end = (size_t)ret;
    }

    return (int)stream->buf[stream->buf_pos++];
}

char *fgets(char *s, int size, FILE *stream) {
    if (!s || size <= 0 || !stream)
        return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0)
                return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *stream) {
    if (!stream)
        return EOF;
    char ch = (char)c;
    ssize_t ret = write(stream->fd, &ch, 1);
    return (ret == 1) ? (unsigned char)c : EOF;
}

int fputs(const char *s, FILE *stream) {
    if (!s || !stream)
        return EOF;
    size_t len = strlen(s);
    return (fwrite(s, 1, len, stream) == len) ? (int)len : EOF;
}

int ungetc(int c, FILE *stream) {
    if (!stream || c == EOF)
        return EOF;
    stream->unget = c;
    stream->has_unget = 1;
    stream->eof = 0;
    return c;
}

ssize_t getdelim(char **lineptr, size_t *n, int delimiter, FILE *stream) {
    if (!lineptr || !n || !stream)
        return -1;
    if (!*lineptr || *n == 0) {
        *n = 128;
        *lineptr = (char *)malloc(*n);
        if (!*lineptr)
            return -1;
    }

    size_t pos = 0;
    while (1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (pos == 0)
                return -1;
            break;
        }

        if (pos + 2 >= *n) {
            size_t new_n = *n * 2;
            char *new_ptr = (char *)realloc(*lineptr, new_n);
            if (!new_ptr)
                return -1;
            *lineptr = new_ptr;
            *n = new_n;
        }

        (*lineptr)[pos++] = (char)c;
        if (c == delimiter)
            break;
    }
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    return getdelim(lineptr, n, '\n', stream);
}

static const char *hex_digits_lower = "0123456789abcdef";
static const char *hex_digits_upper = "0123456789ABCDEF";

static int format_uint(char *buf, size_t size, uint64_t val, int base, int width, char pad, bool left_align,
                       bool uppercase) {
    const char *digits = uppercase ? hex_digits_upper : hex_digits_lower;
    char tmp[65];
    int pos = 0;

    if (val == 0) {
        tmp[pos++] = '0';
    } else {
        while (val > 0) {
            tmp[pos++] = digits[val % base];
            val /= base;
        }
    }

    int written = 0;
    int pad_len = width - pos;
    if (pad_len < 0)
        pad_len = 0;

    if (!left_align) {
        for (int i = 0; i < pad_len && written + 1 < (int)size; i++) {
            buf[written++] = pad;
        }
    }

    for (int i = pos - 1; i >= 0 && written + 1 < (int)size; i--) {
        buf[written++] = tmp[i];
    }

    if (left_align) {
        for (int i = 0; i < pad_len && written + 1 < (int)size; i++) {
            buf[written++] = ' ';
        }
    }

    buf[written] = '\0';
    return written;
}

static int format_int(char *buf, size_t size, int64_t val, int width, char pad, bool left_align) {
    int written = 0;
    if (val < 0) {
        if (written + 1 < (int)size)
            buf[written++] = '-';
        val = -val;
        if (width > 0)
            width--;
    }
    written += format_uint(buf + written, size - written, (uint64_t)val, 10, width, pad, left_align, false);
    return written;
}

static int format_str(char *buf, size_t size, const char *s, int width, int precision, bool left_align) {
    if (!s)
        s = "(null)";
    int len = (int)strlen(s);
    if (precision >= 0 && len > precision)
        len = precision;

    int written = 0;
    int pad_len = width - len;
    if (pad_len < 0)
        pad_len = 0;

    if (!left_align) {
        for (int i = 0; i < pad_len && written + 1 < (int)size; i++) {
            buf[written++] = ' ';
        }
    }

    for (int i = 0; i < len && written + 1 < (int)size; i++) {
        buf[written++] = s[i];
    }

    if (left_align) {
        for (int i = 0; i < pad_len && written + 1 < (int)size; i++) {
            buf[written++] = ' ';
        }
    }

    buf[written] = '\0';
    return written;
}

static int format_float(char *buf, size_t size, double val, int width, int precision, bool left_align) {
    if (precision < 0)
        precision = 6;
    if (precision > 20)
        precision = 20;

    if (isnan(val)) {
        return format_str(buf, size, "nan", width, -1, left_align);
    }
    if (isinf(val)) {
        return format_str(buf, size, (val < 0) ? "-inf" : "inf", width, -1, left_align);
    }

    char tmp[128];
    int pos = 0;

    bool negative = (val < 0.0);
    if (negative) {
        val = -val;
    }

    /* Round to the requested precision */
    double round_adder = 0.5;
    for (int i = 0; i < precision; i++) {
        round_adder /= 10.0;
    }
    val += round_adder;

    unsigned long long int_part = (unsigned long long)val;
    double frac_part = val - (double)int_part;

    /* Integer part */
    char int_buf[64];
    int int_pos = 0;
    if (int_part == 0) {
        int_buf[int_pos++] = '0';
    } else {
        while (int_part > 0) {
            int_buf[int_pos++] = (char)('0' + (int_part % 10));
            int_part /= 10;
        }
    }

    if (negative) {
        tmp[pos++] = '-';
    }

    for (int i = int_pos - 1; i >= 0; i--) {
        tmp[pos++] = int_buf[i];
    }

    if (precision > 0) {
        tmp[pos++] = '.';
        for (int i = 0; i < precision; i++) {
            frac_part *= 10.0;
            int digit = (int)frac_part;
            if (digit > 9)
                digit = 9;
            tmp[pos++] = (char)('0' + digit);
            frac_part -= (double)digit;
        }
    }
    tmp[pos] = '\0';

    return format_str(buf, size, tmp, width, -1, left_align);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    size_t out = 0;
    size_t total_needed = 0;
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            if (buf && size > 0 && out + 1 < size) {
                buf[out++] = *p;
            }
            total_needed++;
            p++;
            continue;
        }

        p++; /* Skip '%' */
        if (*p == '%') {
            if (buf && size > 0 && out + 1 < size) {
                buf[out++] = '%';
            }
            total_needed++;
            p++;
            continue;
        }

        bool left_align = false;
        char pad = ' ';
        int width = 0;
        int precision = -1;
        bool is_long = false;

        /* Parse flags */
        while (*p == '-' || *p == '0' || *p == '+' || *p == ' ' || *p == '#') {
            if (*p == '-')
                left_align = true;
            else if (*p == '0' && !left_align)
                pad = '0';
            p++;
        }

        if (left_align)
            pad = ' ';

        /* Parse width */
        if (*p == '*') {
            int w = va_arg(args, int);
            if (w < 0) {
                left_align = true;
                width = -w;
            } else {
                width = w;
            }
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
        }

        /* Parse precision */
        if (*p == '.') {
            p++;
            if (*p == '*') {
                precision = va_arg(args, int);
                p++;
            } else {
                precision = 0;
                while (*p >= '0' && *p <= '9') {
                    precision = precision * 10 + (*p - '0');
                    p++;
                }
            }
        }

        /* Parse length modifier */
        if (*p == 'l') {
            is_long = true;
            p++;
            if (*p == 'l')
                p++;
        } else if (*p == 'z' || *p == 'j' || *p == 't') {
            is_long = true;
            p++;
        } else if (*p == 'h') {
            p++;
            if (*p == 'h')
                p++;
        }

        char item_buf[512];
        int item_len = 0;

        /* Specifier */
        switch (*p) {
        case 'c': {
            char c = (char)va_arg(args, int);
            item_buf[0] = c;
            item_buf[1] = '\0';
            item_len = 1;
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            item_len = format_str(item_buf, sizeof(item_buf), s, width, precision, left_align);
            break;
        }
        case 'd':
        case 'i': {
            int64_t val = is_long ? va_arg(args, int64_t) : (int64_t)va_arg(args, int);
            item_len = format_int(item_buf, sizeof(item_buf), val, width, pad, left_align);
            break;
        }
        case 'o': {
            uint64_t val = is_long ? va_arg(args, uint64_t) : (uint64_t)va_arg(args, unsigned int);
            item_len = format_uint(item_buf, sizeof(item_buf), val, 8, width, pad, left_align, false);
            break;
        }
        case 'u': {
            uint64_t val = is_long ? va_arg(args, uint64_t) : (uint64_t)va_arg(args, unsigned int);
            item_len = format_uint(item_buf, sizeof(item_buf), val, 10, width, pad, left_align, false);
            break;
        }
        case 'x':
        case 'X':
        case 'p': {
            bool upper = (*p == 'X');
            uint64_t val = (*p == 'p') ? (uintptr_t)va_arg(args, void *)
                                       : (is_long ? va_arg(args, uint64_t) : (uint64_t)va_arg(args, unsigned int));
            int prefix_len = 0;
            if (*p == 'p') {
                item_buf[0] = '0';
                item_buf[1] = 'x';
                prefix_len = 2;
            }
            item_len = prefix_len + format_uint(item_buf + prefix_len, sizeof(item_buf) - prefix_len, val, 16, width,
                                                pad, left_align, upper);
            break;
        }
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'e':
        case 'E': {
            double val = va_arg(args, double);
            item_len = format_float(item_buf, sizeof(item_buf), val, width, precision, left_align);
            break;
        }
        default: {
            if (*p) {
                item_buf[0] = *p;
                item_buf[1] = '\0';
                item_len = 1;
            }
            break;
        }
        }

        for (int i = 0; i < item_len; i++) {
            if (buf && size > 0 && out + 1 < size) {
                buf[out++] = item_buf[i];
            }
            total_needed++;
        }

        if (*p)
            p++;
    }

    if (buf && size > 0) {
        buf[out] = '\0';
    }
    return (int)total_needed;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int len = vsnprintf(str, size, format, args);
    va_end(args);
    return len;
}

int sprintf(char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int len = vsnprintf(str, 1048576, format, args);
    va_end(args);
    return len;
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, 1048576, format, ap);
}

int vprintf(const char *format, va_list ap) {
    return vfprintf(stdout, format, ap);
}

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int len = vprintf(format, args);
    va_end(args);
    return len;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[4096];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    if (len > 0) {
        fwrite(buf, 1, len, stream);
    }
    return len;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int len = vfprintf(stream, format, args);
    va_end(args);
    return len;
}

int vdprintf(int fd, const char *format, va_list ap) {
    char buf[4096];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    if (len > 0) {
        write(fd, buf, len);
    }
    return len;
}

int dprintf(int fd, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int len = vdprintf(fd, format, args);
    va_end(args);
    return len;
}

int vasprintf(char **strp, const char *format, va_list ap) {
    char tmp[1];
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int len = vsnprintf(tmp, 1, format, ap_copy);
    va_end(ap_copy);

    if (len < 0)
        return -1;
    char *buf = (char *)malloc(len + 1);
    if (!buf)
        return -1;

    vsnprintf(buf, len + 1, format, ap);
    *strp = buf;
    return len;
}

int asprintf(char **strp, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int len = vasprintf(strp, format, args);
    va_end(args);
    return len;
}

int puts(const char *s) {
    int len = strlen(s);
    write(STDOUT_FILENO, s, len);
    write(STDOUT_FILENO, "\n", 1);
    return len + 1;
}

int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return (unsigned char)c;
}

int getchar(void) {
    char c;
    ssize_t bytes = read(STDIN_FILENO, &c, 1);
    return (bytes == 1) ? (unsigned char)c : EOF;
}

void perror(const char *s) {
    if (s && *s) {
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    } else {
        fprintf(stderr, "%s\n", strerror(errno));
    }
}

int sscanf(const char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int count = vsscanf(str, format, args);
    va_end(args);
    return count;
}

int vsscanf(const char *str, const char *format, va_list ap) {
    if (!str || !format)
        return 0;
    int count = 0;
    const char *s = str;
    const char *f = format;

    while (*f && *s) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*f))
                f++;
            while (isspace((unsigned char)*s))
                s++;
            continue;
        }

        if (*f != '%') {
            if (*f++ != *s++)
                return count;
            continue;
        }

        f++; /* Skip '%' */
        if (*f == '%') {
            if (*s++ != '%')
                return count;
            f++;
            continue;
        }

        int is_long = 0;
        if (*f == 'l') {
            is_long = 1;
            f++;
        }

        switch (*f) {
        case 'd':
        case 'i': {
            char *end;
            long val = strtol(s, &end, 10);
            if (end == s)
                return count;
            if (is_long) {
                long *p = va_arg(ap, long *);
                *p = val;
            } else {
                int *p = va_arg(ap, int *);
                *p = (int)val;
            }
            s = end;
            count++;
            break;
        }
        case 'u': {
            char *end;
            unsigned long val = strtoul(s, &end, 10);
            if (end == s)
                return count;
            if (is_long) {
                unsigned long *p = va_arg(ap, unsigned long *);
                *p = val;
            } else {
                unsigned int *p = va_arg(ap, unsigned int *);
                *p = (unsigned int)val;
            }
            s = end;
            count++;
            break;
        }
        case 'x':
        case 'X':
        case 'p': {
            char *end;
            unsigned long val = strtoul(s, &end, 16);
            if (end == s)
                return count;
            if (is_long || *f == 'p') {
                unsigned long *p = va_arg(ap, unsigned long *);
                *p = val;
            } else {
                unsigned int *p = va_arg(ap, unsigned int *);
                *p = (unsigned int)val;
            }
            s = end;
            count++;
            break;
        }
        case 'o': {
            char *end;
            unsigned long val = strtoul(s, &end, 8);
            if (end == s)
                return count;
            if (is_long) {
                unsigned long *p = va_arg(ap, unsigned long *);
                *p = val;
            } else {
                unsigned int *p = va_arg(ap, unsigned int *);
                *p = (unsigned int)val;
            }
            s = end;
            count++;
            break;
        }
        case 's': {
            char *p = va_arg(ap, char *);
            while (isspace((unsigned char)*s))
                s++;
            int i = 0;
            while (*s && !isspace((unsigned char)*s)) {
                p[i++] = *s++;
            }
            p[i] = '\0';
            if (i > 0)
                count++;
            break;
        }
        case 'c': {
            char *p = va_arg(ap, char *);
            *p = *s++;
            count++;
            break;
        }
        case '[': {
            f++;
            int invert = 0;
            if (*f == '^') {
                invert = 1;
                f++;
            }
            unsigned char set[256] = {0};
            if (*f == ']') {
                set[(unsigned char)*f++] = 1;
            }
            while (*f && *f != ']') {
                if (*f == '-' && *(f - 1) != '[' && *(f + 1) != ']' && *(f + 1) != '\0') {
                    unsigned char start = (unsigned char)*(f - 1);
                    unsigned char end = (unsigned char)*(f + 1);
                    for (unsigned int c = start; c <= end; c++) {
                        set[(unsigned char)c] = 1;
                    }
                    f += 2;
                } else {
                    set[(unsigned char)*f++] = 1;
                }
            }
            char *p = va_arg(ap, char *);
            int i = 0;
            while (*s) {
                unsigned char c = (unsigned char)*s;
                if (invert) {
                    if (set[c])
                        break;
                } else {
                    if (!set[c])
                        break;
                }
                p[i++] = *s++;
            }
            p[i] = '\0';
            if (i > 0)
                count++;
            else
                return count;
            break;
        }
        default:
            return count;
        }
        f++;
    }
    return count;
}

int fscanf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int count = vfscanf(stream, format, args);
    va_end(args);
    return count;
}

int vfscanf(FILE *stream, const char *format, va_list ap) {
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stream))
        return EOF;
    return vsscanf(buf, format, ap);
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    (void)stream;
    (void)buf;
    (void)mode;
    (void)size;
    return 0;
}

void setbuf(FILE *stream, char *buf) {
    setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

int rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) {
        errno = EINVAL;
        return -1;
    }
    int64_t ret = __syscall2(SYS_rename, (int64_t)oldpath, (int64_t)newpath);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return 0;
}

int remove(const char *pathname) {
    if (!pathname) {
        errno = EINVAL;
        return -1;
    }
    int64_t r = __syscall1(SYS_unlink, (int64_t)pathname);
    if (r < 0) {
        r = __syscall1(SYS_rmdir, (int64_t)pathname);
    }
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

FILE *popen(const char *command, const char *type) {
    (void)command;
    (void)type;
    return NULL;
}

int pclose(FILE *stream) {
    if (stream) fclose(stream);
    return 0;
}

#undef getc
#undef putc

int getc(FILE *stream) {
    return fgetc(stream);
}

int putc(int c, FILE *stream) {
    return fputc(c, stream);
}

int fgetpos(FILE *stream, fpos_t *pos) {
    if (!stream || !pos) {
        errno = EINVAL;
        return -1;
    }
    long o = ftell(stream);
    if (o < 0) return -1;
    *pos = (fpos_t)o;
    return 0;
}

int fsetpos(FILE *stream, const fpos_t *pos) {
    if (!stream || !pos) {
        errno = EINVAL;
        return -1;
    }
    return fseek(stream, (long)*pos, SEEK_SET);
}

FILE *tmpfile(void) {
    return fopen("/tmp/tmpfile", "w+b");
}

int vscanf(const char *format, va_list ap) {
    return vfscanf(stdin, format, ap);
}

int scanf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vscanf(format, ap);
    va_end(ap);
    return ret;
}
