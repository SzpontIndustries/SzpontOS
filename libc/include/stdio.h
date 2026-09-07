#ifndef _STDIO_H
#define _STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdarg.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define BUFSIZ 4096
#define P_tmpdir "/tmp"

typedef struct _FILE {
    int fd;
    int flags;
    int error;
    int eof;
    int unget;
    int has_unget;
    unsigned char *buf;
    size_t buf_size;
    size_t buf_pos;
    size_t buf_end;
    int buf_mode;
    int own_buf;
} FILE;

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

void setbuf(FILE *stream, char *buf);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define stdin stdin
#define stdout stdout
#define stderr stderr

FILE *fopen(const char *pathname, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *pathname, const char *mode, FILE *stream);
FILE *popen(const char *command, const char *type);
int pclose(FILE *stream);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fseeko(FILE *stream, off_t offset, int whence);
off_t ftello(FILE *stream);
void rewind(FILE *stream);
int fflush(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fileno(FILE *stream);

typedef off_t fpos_t;

int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);

int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int ungetc(int c, FILE *stream);

int getc(FILE *stream);
int putc(int c, FILE *stream);
FILE *tmpfile(void);

#ifndef __cplusplus
#define getc(stream) fgetc(stream)
#define putc(c, stream) fputc(c, stream)
#endif

ssize_t getdelim(char **lineptr, size_t *n, int delimiter, FILE *stream);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);

int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int vprintf(const char *format, va_list ap);
int fprintf(FILE *stream, const char *format, ...) __attribute__((format(printf, 2, 3)));
int vfprintf(FILE *stream, const char *format, va_list ap);
int sprintf(char *str, const char *format, ...) __attribute__((format(printf, 2, 3)));
int vsprintf(char *str, const char *format, va_list ap);
int snprintf(char *str, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int dprintf(int fd, const char *format, ...) __attribute__((format(printf, 2, 3)));
int vdprintf(int fd, const char *format, va_list ap);
int asprintf(char **strp, const char *format, ...) __attribute__((format(printf, 2, 3)));
int vasprintf(char **strp, const char *format, va_list ap);

int scanf(const char *format, ...) __attribute__((format(scanf, 1, 2)));
int vscanf(const char *format, va_list ap);
int sscanf(const char *str, const char *format, ...);
int vsscanf(const char *str, const char *format, va_list ap);
int fscanf(FILE *stream, const char *format, ...);
int vfscanf(FILE *stream, const char *format, va_list ap);

int puts(const char *s);
int putchar(int c);
int getchar(void);
void perror(const char *s);

int rename(const char *oldpath, const char *newpath);
int remove(const char *pathname);

#define setlinebuf(stream) setvbuf((stream), NULL, _IOLBF, 0)
#define setbuffer(stream, buf, size) setvbuf((stream), (buf), (buf) ? _IOFBF : _IONBF, (size))

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
