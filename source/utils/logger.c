/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/logger.h"
#include "utils/utils.h"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>

#define COLOR_RED    "\x1B[38;5;196m"
#define COLOR_PINK   "\x1B[38;5;212m"
#define COLOR_ORANGE "\x1B[38;5;202m"
#define COLOR_BLUE   "\x1B[38;5;32m"
#define COLOR_GREEN  "\x1B[32m"
#define COLOR_CYAN   "\x1B[36m"

#define COLOR_END    "\033[0m"

static SceKernelLwMutexWork _log_mutex;
static atomic_bool _log_mutex_ready = ATOMIC_VAR_INIT(false);

// Buffer A is used to adjust the format string.
static char buffer_a[2048];
// Buffer B is used to compile the final log using the updated format string.
static char buffer_b[2048];
// Plain (no ANSI colors), incrementally-flushed copy of buffer_b, written to the
// file/UDP sinks so a crash mid-session doesn't lose whatever was logged before it.
static char buffer_plain[2048];

static SceUID _log_file_fd = -1;

// Batches file writes so a hot loop logging at DEBUG level (e.g. a directory scan with
// thousands of entries -- this is what turned a `models/` folder listing into a minutes-
// long stall once every readdir() call did its own synchronous sceIoWrite) doesn't turn
// into one storage syscall per line. WARN/ERROR/FATAL and port_trace() still flush
// immediately.
//
// A real hardware crash (data abort / undefined instruction) never runs fatal_error(),
// so it never runs logger_flush() either -- whatever's still sitting in this buffer at
// that moment is gone from the log. Confirmed the hard way: a run that crashed deep in
// shader loading produced a log with NONE of the shader-loading debug lines, because
// they were all still buffered. So on top of the size-based flush, also flush every
// LOG_FILE_FLUSH_EVERY lines regardless of level, to bound how much a crash can lose.
#define LOG_FILE_BUFFER_SIZE 4096
#define LOG_FILE_FLUSH_EVERY 8
static char _log_file_buffer[LOG_FILE_BUFFER_SIZE];
static size_t _log_file_buffer_len = 0;
static unsigned int _log_file_pending_lines = 0;

#ifdef UDP_LOG_HOST
static int _log_udp_socket = -1;
static SceNetSockaddrIn _log_udp_addr;
static bool _log_udp_ready = false;
#endif

static void _log_lock(void) {
    if (!atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        int ret = sceKernelCreateLwMutex(&_log_mutex, "log_lock", 0, 0, NULL);
        if (ret < 0) {
            sceClibPrintf("Error: failed to create log mutex: 0x%x\n", ret);
            return;
        }
        atomic_store_explicit(&_log_mutex_ready, true, memory_order_relaxed);
    }
    sceKernelLockLwMutex(&_log_mutex, 1, NULL);
}

static void _log_unlock(void) {
    if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        sceKernelUnlockLwMutex(&_log_mutex, 1);
    }
}

void logger_flush(void) {
    if (_log_file_fd >= 0 && _log_file_buffer_len > 0) {
        sceIoWrite(_log_file_fd, _log_file_buffer, _log_file_buffer_len);
        _log_file_buffer_len = 0;
    }
    _log_file_pending_lines = 0;
}

// Writes a line to whichever sinks are currently available. The file write goes through
// the small RAM buffer above (see logger_flush()); `important` (WARN/ERROR/FATAL and
// port_trace()) forces it out to disk right away, anything else just accumulates until
// the buffer fills up. The UDP send is fire-and-forget best-effort, sent per line as-is.
static void _log_write_sinks(const char *line, size_t len, bool important) {
    if (_log_file_fd >= 0) {
        if (len >= sizeof(_log_file_buffer)) {
            logger_flush();
            sceIoWrite(_log_file_fd, line, len);
        } else {
            if (_log_file_buffer_len + len > sizeof(_log_file_buffer)) {
                logger_flush();
            }
            memcpy(_log_file_buffer + _log_file_buffer_len, line, len);
            _log_file_buffer_len += len;
            _log_file_pending_lines++;
            if (important || _log_file_pending_lines >= LOG_FILE_FLUSH_EVERY) {
                logger_flush();
            }
        }
    }

#ifdef UDP_LOG_HOST
    if (_log_udp_ready) {
        sceNetSendto(_log_udp_socket, line, len, 0,
                     (SceNetSockaddr *) &_log_udp_addr, sizeof(_log_udp_addr));
    }
#endif
}

void logger_init(void) {
    char path[256];
    sceClibSnprintf(path, sizeof(path), DATA_PATH "logs/game_log_%llu.txt",
                     (unsigned long long) current_timestamp_ms());
    file_mkpath(path, 0777);

    _log_file_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (_log_file_fd < 0) {
        sceClibPrintf("logger_init: could not open log file \"%s\" (0x%x)\n",
                      path, _log_file_fd);
    }

#ifdef UDP_LOG_HOST
    SceNetInitParam net_init_param;
    static uint8_t net_mem[256 * 1024];
    net_init_param.memory = net_mem;
    net_init_param.size = sizeof(net_mem);
    net_init_param.flags = 0;

    int ret = sceNetCtlInit();
    if (ret < 0) {
        sceClibPrintf("logger_init: sceNetCtlInit failed (0x%x), UDP log disabled\n", ret);
        return;
    }

    ret = sceNetInit(&net_init_param);
    if (ret < 0) {
        sceClibPrintf("logger_init: sceNetInit failed (0x%x), UDP log disabled\n", ret);
        return;
    }

    _log_udp_socket = sceNetSocket("game_log_udp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    if (_log_udp_socket < 0) {
        sceClibPrintf("logger_init: sceNetSocket failed (0x%x), UDP log disabled\n",
                      _log_udp_socket);
        return;
    }

    memset(&_log_udp_addr, 0, sizeof(_log_udp_addr));
    _log_udp_addr.sin_family = SCE_NET_AF_INET;
    _log_udp_addr.sin_port = sceNetHtons(UDP_LOG_PORT);
    sceNetInetPton(SCE_NET_AF_INET, UDP_LOG_HOST, &_log_udp_addr.sin_addr);

    _log_udp_ready = true;
    sceClibPrintf("logger_init: streaming logs via UDP to %s:%d\n", UDP_LOG_HOST, UDP_LOG_PORT);
#endif
}

void _log_print(int t, const char* fmt, ...) {
    _log_lock();

    const char *plain_tag;
    switch (t) {
        case LT_DEBUG:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s• debug%s    %s\n",
                            COLOR_PINK, COLOR_END, fmt);
            plain_tag = "debug"; break;
        case LT_INFO:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %sℹ info%s     %s\n",
                            COLOR_BLUE, COLOR_END, fmt);
            plain_tag = "info"; break;
        case LT_WARN:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⚠ warning%s  %s\n",
                            COLOR_ORANGE, COLOR_END, fmt);
            plain_tag = "warning"; break;
        case LT_ERROR:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⨯ error%s    %s\n",
                            COLOR_RED, COLOR_END, fmt);
            plain_tag = "error"; break;
        case LT_FATAL:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! fatal%s    %s\n",
                            COLOR_RED, COLOR_END, fmt);
            plain_tag = "fatal"; break;
        case LT_SUCCESS:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! success%s  %s\n",
                            COLOR_GREEN, COLOR_END, fmt);
            plain_tag = "success"; break;
        case LT_WAIT:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s… waiting%s  %s\n",
                            COLOR_CYAN, COLOR_END, fmt);
            plain_tag = "waiting"; break;
        default:
            _log_unlock();
            return;
    }

    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(buffer_b, sizeof(buffer_b), buffer_a, list);
    va_end(list);
    sceClibPrintf("%s", buffer_b);

    va_start(list, fmt);
    int plain_len = sceClibSnprintf(buffer_plain, sizeof(buffer_plain), "[%s] ", plain_tag);
    if (plain_len > 0 && (size_t) plain_len < sizeof(buffer_plain)) {
        plain_len += sceClibVsnprintf(buffer_plain + plain_len,
                                       sizeof(buffer_plain) - (size_t) plain_len, fmt, list);
    }
    va_end(list);
    if (plain_len > 0) {
        if ((size_t) plain_len >= sizeof(buffer_plain)) {
            plain_len = sizeof(buffer_plain) - 1;
        }
        buffer_plain[plain_len] = '\n';
        bool important = (t == LT_WARN || t == LT_ERROR || t == LT_FATAL);
        _log_write_sinks(buffer_plain, (size_t) plain_len + 1, important);
    }

    _log_unlock();
}

void port_trace(const char *fmt, ...) {
    _log_lock();

    char buf[1024];
    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(buf, sizeof(buf), fmt, list);
    va_end(list);
    sceClibPrintf("[port_trace] %s\n", buf);

    int len = sceClibSnprintf(buffer_plain, sizeof(buffer_plain), "[port_trace] %s\n", buf);
    if (len > 0) {
        if ((size_t) len >= sizeof(buffer_plain)) {
            len = sizeof(buffer_plain) - 1;
        }
        // FalsoJNI's own messages are already filtered to warn/error severity
        // (see FalsoJNI_Logger.c) by the time they reach port_trace(), so always flush.
        _log_write_sinks(buffer_plain, (size_t) len, true);
    }

    _log_unlock();
}

