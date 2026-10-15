/* Logging to a file, a nonblocking UDP LAN broadcast on port 9023, and
 * system notifications.
 *
 * The file is opened in append mode and flushed after
 * every line: a run that ends in a crash still leaves everything up to the
 * crash on disk, which is what made the difference in diagnosing this one.
 */
#ifndef FGG_LOG_H
#define FGG_LOG_H

/* Opens the log at `path`, creating `dir` if needed. Returns 0 on failure, in
 * which case the logging calls below are harmless no-ops. */
int log_open(const char *dir, const char *path);
int log_open_port(const char *dir, const char *path, unsigned port);
void log_close(void);
void log_poll(void);
int log_stop_requested(void);

void log_line(const char *fmt, ...);

/* Release builds omit diagnostic formatting and its argument evaluation. */
#ifndef XINPUT_VERBOSE
#define XINPUT_VERBOSE 0
#endif
#if XINPUT_VERBOSE
#define log_debug(...) log_line(__VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif

/* Puts a message in the console's notification area. */
void notify(const char *fmt, ...);

#endif
