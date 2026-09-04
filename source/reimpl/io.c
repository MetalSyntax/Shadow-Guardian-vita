/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static const char *try_fallback_1_7(const char *path, char *buffer, size_t size) {
    if (!path) return NULL;
    const char *p = strstr(path, "_1_7");
    if (!p) return NULL;

    snprintf(buffer, size, "%s", path);
    char *sub = strstr(buffer, "_1_7");
    if (sub) {
        sub[3] = '6';
        if (file_exists(buffer)) {
            l_info("Fallback redirect: %s -> %s", path, buffer);
            return buffer;
        }
    }
    return NULL;
}

static const char *translate_path(const char *path, char *buffer, size_t size) {
    if (!path || !path[0]) return path;

    // Direct Vita device paths
    if (strncmp(path, "ux0:", 4) == 0 || strncmp(path, "app0:", 5) == 0 ||
        strncmp(path, "ur0:", 4) == 0 || strncmp(path, "uma0:", 5) == 0) {
        if (file_exists(path)) return path;

        const char *fb = try_fallback_1_7(path, buffer, size);
        if (fb) return fb;

        return path;
    }

    if (strcmp(path, "/proc/cpuinfo") == 0) return "app0:/cpuinfo";
    if (strcmp(path, "/proc/meminfo") == 0) return "app0:/meminfo";

    // Handle Android sdcard path
    static const char sdcard_prefix[] = "/sdcard/gameloft/games/GloftSGHP";
    static const char mnt_sdcard_prefix[] = "/mnt/sdcard/gameloft/games/GloftSGHP";
    static const char data_data_prefix[] = "/data/data/com.gameloft.android.ANMP.GloftSGHP.ML";

    const char *rel = NULL;
    if (strncasecmp(path, sdcard_prefix, sizeof(sdcard_prefix) - 1) == 0) {
        rel = path + sizeof(sdcard_prefix) - 1;
    } else if (strncasecmp(path, mnt_sdcard_prefix, sizeof(mnt_sdcard_prefix) - 1) == 0) {
        rel = path + sizeof(mnt_sdcard_prefix) - 1;
    }

    if (rel) {
        while (*rel == '/') rel++;
        if (*rel == '\0') {
            snprintf(buffer, size, "%s", DATA_PATH);
            return buffer;
        }
        // First check directly in DATA_PATH
        snprintf(buffer, size, "%s%s", DATA_PATH, rel);
        if (file_exists(buffer)) return buffer;

        // Check in DATA_PATH/GloftSGHP/
        char test[PATH_MAX];
        snprintf(test, sizeof(test), "%sGloftSGHP/%s", DATA_PATH, rel);
        if (file_exists(test)) {
            strncpy(buffer, test, size);
            buffer[size - 1] = '\0';
            return buffer;
        }

        const char *fb = try_fallback_1_7(buffer, test, sizeof(test));
        if (fb) {
            strncpy(buffer, fb, size);
            buffer[size - 1] = '\0';
            return buffer;
        }

        return buffer;
    }

    if (strncasecmp(path, data_data_prefix, sizeof(data_data_prefix) - 1) == 0) {
        rel = path + sizeof(data_data_prefix) - 1;
        while (*rel == '/') rel++;
        if (*rel == '\0') {
            snprintf(buffer, size, "%s", DATA_PATH);
            return buffer;
        }
        snprintf(buffer, size, "%ssaves/%s", DATA_PATH, rel);
        return buffer;
    }

    if (strncasecmp(path, "GloftSGHP/", 10) == 0) {
        rel = path + 10;
        while (*rel == '/') rel++;
        snprintf(buffer, size, "%s%s", DATA_PATH, rel);
        if (file_exists(buffer)) return buffer;
        snprintf(buffer, size, "%sGloftSGHP/%s", DATA_PATH, rel);
        if (file_exists(buffer)) return buffer;

        char test[PATH_MAX];
        const char *fb = try_fallback_1_7(buffer, test, sizeof(test));
        if (fb) {
            strncpy(buffer, fb, size);
            buffer[size - 1] = '\0';
            return buffer;
        }

        return buffer;
    }

    // Relative path check
    if (path[0] != '/' && strchr(path, ':') == NULL) {
        char test[PATH_MAX];
        snprintf(test, sizeof(test), "%s%s", DATA_PATH, path);
        if (file_exists(test)) {
            strncpy(buffer, test, size);
            buffer[size - 1] = '\0';
            return buffer;
        }
        snprintf(test, sizeof(test), "%sGloftSGHP/%s", DATA_PATH, path);
        if (file_exists(test)) {
            strncpy(buffer, test, size);
            buffer[size - 1] = '\0';
            return buffer;
        }

        snprintf(test, sizeof(test), "%s%s", DATA_PATH, path);
        const char *fb = try_fallback_1_7(test, buffer, size);
        if (fb) return fb;

        snprintf(test, sizeof(test), "%sGloftSGHP/%s", DATA_PATH, path);
        fb = try_fallback_1_7(test, buffer, size);
        if (fb) return fb;
    }

    return path;
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    char trans_buf[PATH_MAX];
    const char *real_path = translate_path(filename, trans_buf, sizeof(trans_buf));

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(real_path, mode);
#else
    FILE* ret = fopen(real_path, mode);
#endif

    if (ret)
        l_debug("fopen(%s -> %s, %s): %p", filename, real_path, mode, ret);
    else
        l_warn("fopen(%s -> %s, %s): FAILED", filename, real_path, mode);

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    char trans_buf[PATH_MAX];
    const char *real_path = translate_path(path, trans_buf, sizeof(trans_buf));

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(real_path, oflag, mode);
    if (ret >= 0)
        l_debug("open(%s -> %s, %x): %i", path, real_path, oflag, ret);
    else
        l_warn("open(%s -> %s, %x): FAILED", path, real_path, oflag);
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0) {
        // Bug #16 (cinematic/gameplay near-black textures, port_progress.md
        // Bug #15/#16): pig::stream::MMap() does open()+fstat()+mmap() and
        // uses st_blksize (patched to mirror st_size, see stat_newlib_to_bionic
        // below) as the mmap() length. That worked for ~666 of the menu's
        // textures (Bug #13) but ~106 remaining textures -- all of them loaded
        // later, during level/cinematic streaming rather than at the menu --
        // still got mmap()'d with absurdly small lengths (1, 0, 10 bytes seen
        // in game_log_1788552823938.txt for cin_a01.tga/cin_t03.tga/
        // cin_d03.tga, all real files tens/hundreds of KB in size).
        //
        // The game's own MMap() also does a redundant path-based stat() +
        // fopen() on the very same file right after this fstat() call, and
        // that path-based stat() always reports success -- so the file is
        // genuinely present with a real size on ux0:, and only the *fd-based*
        // fstat() is unreliable for it. On the Vita's newlib, fstat(fd, ...)
        // is backed by sceIoGetstatByFd(), a separate syscall from the
        // path-based sceIoGetstat() that stat()/stat_soloader() use, and it
        // can report a stale/short st_size for a handle that was just
        // opened (more likely to be hit under the heavier concurrent I/O of
        // level/cinematic streaming than at the menu). lseek()-based sizing
        // goes through sceIoLseek() instead, which doesn't share that issue,
        // so use it to get the real size and let stat_newlib_to_bionic()'s
        // st_blksize=st_size hack (below) work of off a trustworthy value.
        off_t cur = lseek(fd, 0, SEEK_CUR);
        if (cur != (off_t) -1) {
            off_t end = lseek(fd, 0, SEEK_END);
            lseek(fd, cur, SEEK_SET);
            if (end != (off_t) -1 && end > st.st_size) {
                l_debug("fstat(%i): st_size=%lld looked stale, using lseek "
                        "size=%lld instead", fd, (long long) st.st_size,
                        (long long) end);
                st.st_size = end;
            }
        }
        stat_newlib_to_bionic(&st, buf);
    }

    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    char trans_buf[PATH_MAX];
    const char *real_path = translate_path(path, trans_buf, sizeof(trans_buf));

    struct stat st;
    int res = stat(real_path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s -> %s): %i", path, real_path, res);
    return res;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    char trans_buf[PATH_MAX];
    const char *real_path = translate_path(_pathname, trans_buf, sizeof(trans_buf));
    DIR* ret = opendir(real_path);
    l_debug("opendir(\"%s\" -> \"%s\"): %p", _pathname, real_path, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
