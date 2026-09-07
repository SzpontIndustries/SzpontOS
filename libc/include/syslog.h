#ifndef _SYSLOG_H
#define _SYSLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

/* Priorities (values 0-7) */
#define LOG_EMERG   0   /* system is unusable */
#define LOG_ALERT   1   /* action must be taken immediately */
#define LOG_CRIT    2   /* critical conditions */
#define LOG_ERR     3   /* error conditions */
#define LOG_WARNING 4   /* warning conditions */
#define LOG_NOTICE  5   /* normal but significant condition */
#define LOG_INFO    6   /* informational */
#define LOG_DEBUG   7   /* debug-level messages */

#define LOG_PRIMASK 0x07

/* Facility codes */
#define LOG_KERN     (0<<3)   /* kernel messages */
#define LOG_USER     (1<<3)   /* random user-level messages */
#define LOG_MAIL     (2<<3)   /* mail system */
#define LOG_DAEMON   (3<<3)   /* system daemons */
#define LOG_AUTH     (4<<3)   /* security/authorization messages */
#define LOG_SYSLOG   (5<<3)   /* messages generated internally by syslogd */
#define LOG_LPR      (6<<3)   /* line printer subsystem */
#define LOG_NEWS     (7<<3)   /* network news subsystem */
#define LOG_UUCP     (8<<3)   /* UUCP subsystem */
#define LOG_CRON     (9<<3)   /* clock daemon */
#define LOG_AUTHPRIV (10<<3)  /* security/authorization messages (private) */
#define LOG_FTP      (11<<3)  /* ftp daemon */
#define LOG_LOCAL0   (16<<3)  /* reserved for local use */
#define LOG_LOCAL1   (17<<3)  /* reserved for local use */
#define LOG_LOCAL2   (18<<3)  /* reserved for local use */
#define LOG_LOCAL3   (19<<3)  /* reserved for local use */
#define LOG_LOCAL4   (20<<3)  /* reserved for local use */
#define LOG_LOCAL5   (21<<3)  /* reserved for local use */
#define LOG_LOCAL6   (22<<3)  /* reserved for local use */
#define LOG_LOCAL7   (23<<3)  /* reserved for local use */

#define LOG_FACMASK  0x03f8

/* Option flags for openlog */
#define LOG_PID    0x01   /* log the pid with each message */
#define LOG_CONS   0x02   /* log on the console if errors in sending */
#define LOG_ODELAY 0x04   /* delay open until first syslog() (default) */
#define LOG_NDELAY 0x08   /* don't delay open */
#define LOG_NOWAIT 0x10   /* don't wait for console forks: deprecated */
#define LOG_PERROR 0x20   /* log to stderr as well */

#define LOG_MASK(pri) (1 << (pri))
#define LOG_UPTO(pri) ((1 << ((pri)+1)) - 1)

void openlog(const char *ident, int option, int facility);
void syslog(int priority, const char *format, ...);
void vsyslog(int priority, const char *format, va_list ap);
void closelog(void);
int setlogmask(int mask);

#ifdef __cplusplus
}
#endif

#endif /* _SYSLOG_H */
