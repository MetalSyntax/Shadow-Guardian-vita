/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  logger.h
 * @brief Logging utilities.
 */

#ifndef SOLOADER_LOGGER_H
#define SOLOADER_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#define LT_DEBUG   0
#define LT_INFO    1
#define LT_WARN    2
#define LT_ERROR   3
#define LT_FATAL   4
#define LT_SUCCESS 5
#define LT_WAIT    6

#ifdef DEBUG_SOLOADER
#define l_debug(...)   _log_print(LT_DEBUG,   __VA_ARGS__)
#define l_info(...)    _log_print(LT_INFO,    __VA_ARGS__)
#define l_warn(...)    _log_print(LT_WARN,    __VA_ARGS__)
#define l_success(...) _log_print(LT_SUCCESS, __VA_ARGS__)
#define l_wait(...)    _log_print(LT_WAIT,    __VA_ARGS__)
#else
#define l_debug(...)
#define l_info(...)
#define l_warn(...)
#define l_success(...)
#define l_wait(...)
#endif

#define l_error(...)   _log_print(LT_ERROR,   __VA_ARGS__)
#define l_fatal(...)   _log_print(LT_FATAL,   __VA_ARGS__)

void _log_print(int t, const char* fmt, ...)
                __attribute__ ((format (printf, 2, 3)));

/**
 * Set up the log sinks (incremental file log under DATA_PATH"logs/", and,
 * if UDP_LOG_HOST was configured at build time, a best-effort UDP stream for
 * viewing logs live on a PC). Safe to call even if either sink fails to
 * initialize (e.g. no network) -- logging just keeps going to whichever
 * sinks are actually available, plus the console as always.
 *
 * Must be called once, early in main(), before anything else logs.
 */
void logger_init(void);

/**
 * Force any buffered (not yet written) log file contents out to disk right now.
 *
 * The file sink batches writes internally so that high-frequency DEBUG-level
 * logging (e.g. a directory scan with thousands of entries) doesn't turn into
 * thousands of individual storage syscalls -- WARN/ERROR/FATAL lines and
 * port_trace() always flush immediately, but plain INFO/DEBUG/SUCCESS/WAIT
 * lines can sit in the buffer for a bit. Call this before anything that might
 * hang or otherwise not return normally (e.g. fatal_error() already does).
 */
void logger_flush(void);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_LOGGER_H
