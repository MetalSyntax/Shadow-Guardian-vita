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

        // If path is under DATA_PATH, also check DATA_PATH "GloftSGHP/..."
        if (strncmp(buffer, DATA_PATH, sizeof(DATA_PATH) - 1) == 0 &&
            strncmp(buffer + sizeof(DATA_PATH) - 1, "GloftSGHP/", 10) != 0) {
            char gloft_buf[PATH_MAX];
            snprintf(gloft_buf, sizeof(gloft_buf), "%sGloftSGHP/%s",
                     DATA_PATH, buffer + sizeof(DATA_PATH) - 1);
            if (file_exists(gloft_buf)) {
                l_info("Fallback redirect: %s -> %s", path, gloft_buf);
                strncpy(buffer, gloft_buf, size);
                buffer[size - 1] = '\0';
                return buffer;
            }
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

        // patch.c's initPath hook makes the engine believe its whole app
        // path IS DATA_PATH, so every Lib::Open()/asset request the .so
        // makes at runtime arrives here already as a flat "ux0:" path --
        // it never goes through the sdcard_prefix/"GloftSGHP/" branches
        // below. Testers who copy the data files keeping the original
        // Android layout (DATA_PATH "GloftSGHP/<file>") pass init.c's
        // startup check (which explicitly allows that layout) but then
        // fail here silently, since this branch had no equivalent
        // fallback. Mirror it for every asset, not just the _1_7 case.
        if (strncmp(path, DATA_PATH, sizeof(DATA_PATH) - 1) == 0 &&
            strncmp(path + sizeof(DATA_PATH) - 1, "GloftSGHP/", 10) != 0) {
            snprintf(buffer, size, "%sGloftSGHP/%s", DATA_PATH,
                     path + sizeof(DATA_PATH) - 1);
            if (file_exists(buffer)) return buffer;
        }

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

// Perf fix (2026-09-15): the engine's SoundMgr does a full fopen()+fread()+
// fclose() on the *same* short one-shot SFX .wav every single time it's
// triggered, on the main thread (same thread as Game::FrameUpdate(), per
// port_progress.md's Bug #17 SoundMgr::Update() findings) -- confirmed from
// game_log_1789505677296.txt, where walking triggers ~1 of these round-trips
// per footstep but combat (gunfire + simultaneous bullet impacts/ricochets)
// stacks up 5-8+ of them within the same handful of log lines. Each is a
// synchronous stat()+fopen()+fread()+fclose() round-trip to ux0: storage,
// squarely on the frame-critical thread -- a very plausible source of the
// FPS drops reported while shooting/rolling vs. plain walking.
//
// Fix: cache the decoded bytes of small sounds/*.wav files in RAM the first
// time they're actually read from storage, and serve every later "open" of
// the same file from that cached buffer instead of touching storage again.
// Concurrent overlapping voices of the same sound (e.g. several bullet
// impacts at once) each get their own cursor into the *same* read-only
// buffer, matching real independent-fd semantics. Scope is deliberately
// narrow (sounds/ prefix, size-capped) to avoid touching model/texture/
// shader loading, which goes through this same fopen_soloader() but has its
// own (already-debugged) I/O quirks -- see the stat64_bionic/MMap() history
// in this file and io.h.
//
// No mutex guards s_sound_cache/s_cached_handles: source/audio.c's mixer
// thread only ever touches its own PCM ring buffer, never file I/O, and
// every sounds/*.wav open observed in the logs happens on the main thread
// (SoundMgr::Update(), part of Game::FrameUpdate() -- see port_progress.md
// Bug #17). If the engine is ever confirmed to open sounds from another
// thread, this needs a lock around the population/registry mutations below.
#define SOUND_CACHE_MAX_ENTRIES 128
#define SOUND_CACHE_MAX_FILE_SIZE (1 * 1024 * 1024)
#define SOUND_CACHE_MAX_HANDLES 64

typedef struct {
    char path[192];
    uint8_t *data;
    size_t size;
} sound_cache_entry_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t cursor;
} cached_handle_t;

static sound_cache_entry_t s_sound_cache[SOUND_CACHE_MAX_ENTRIES];
static int s_sound_cache_count = 0;
static cached_handle_t *s_cached_handles[SOUND_CACHE_MAX_HANDLES];

static int is_cacheable_sound_open(const char *filename, const char *mode) {
    if (!filename || !mode) return 0;
    if (strncasecmp(filename, "sounds/", 7) != 0) return 0;
    // Only plain read opens -- never intercept write/append/update modes.
    if (strchr(mode, '+') || strchr(mode, 'w') || strchr(mode, 'a')) return 0;
    return 1;
}

static sound_cache_entry_t *find_sound_cache_entry(const char *filename) {
    for (int i = 0; i < s_sound_cache_count; i++) {
        if (strcmp(s_sound_cache[i].path, filename) == 0)
            return &s_sound_cache[i];
    }
    return NULL;
}

static sound_cache_entry_t *load_and_cache_sound(const char *filename, const char *mode) {
    if (s_sound_cache_count >= SOUND_CACHE_MAX_ENTRIES) return NULL;

    char trans_buf[PATH_MAX];
    const char *real_path = translate_path(filename, trans_buf, sizeof(trans_buf));

#ifdef USE_SCELIBC_IO
    FILE *f = sceLibcBridge_fopen(real_path, mode);
#else
    FILE *f = fopen(real_path, mode);
#endif
    if (!f) return NULL;

#ifdef USE_SCELIBC_IO
    sceLibcBridge_fseek(f, 0, SEEK_END);
    long sz = sceLibcBridge_ftell(f);
    sceLibcBridge_fseek(f, 0, SEEK_SET);
#else
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
#endif

    if (sz <= 0 || (size_t)sz > SOUND_CACHE_MAX_FILE_SIZE) {
#ifdef USE_SCELIBC_IO
        sceLibcBridge_fclose(f);
#else
        fclose(f);
#endif
        return NULL;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
#ifdef USE_SCELIBC_IO
        sceLibcBridge_fclose(f);
#else
        fclose(f);
#endif
        return NULL;
    }

#ifdef USE_SCELIBC_IO
    size_t got = sceLibcBridge_fread(buf, 1, (size_t)sz, f);
    sceLibcBridge_fclose(f);
#else
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
#endif

    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }

    sound_cache_entry_t *entry = &s_sound_cache[s_sound_cache_count];
    strncpy(entry->path, filename, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';
    entry->data = buf;
    entry->size = (size_t)sz;
    s_sound_cache_count++;

    l_info("[sound_cache] cached '%s' (%zu bytes, %d/%d slots used)",
           filename, entry->size, s_sound_cache_count, SOUND_CACHE_MAX_ENTRIES);

    return entry;
}

static int find_cached_handle_slot(FILE *f) {
    for (int i = 0; i < SOUND_CACHE_MAX_HANDLES; i++) {
        if ((FILE *)s_cached_handles[i] == f) return i;
    }
    return -1;
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    if (is_cacheable_sound_open(filename, mode)) {
        sound_cache_entry_t *entry = find_sound_cache_entry(filename);
        if (!entry)
            entry = load_and_cache_sound(filename, mode);

        if (entry) {
            int slot = -1;
            for (int i = 0; i < SOUND_CACHE_MAX_HANDLES; i++) {
                if (!s_cached_handles[i]) { slot = i; break; }
            }
            if (slot >= 0) {
                cached_handle_t *h = (cached_handle_t *)malloc(sizeof(cached_handle_t));
                if (h) {
                    h->data = entry->data;
                    h->size = entry->size;
                    h->cursor = 0;
                    s_cached_handles[slot] = h;
                    l_debug("fopen(%s -> [cache, %zu bytes], %s): %p", filename, entry->size, mode, (void *)h);
                    return (FILE *)h;
                }
            }
            // Registry full or alloc failed -- fall through to a real open below.
        }
    }

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

size_t fread_soloader(void *ptr, size_t size, size_t count, FILE *stream) {
    int slot = find_cached_handle_slot(stream);
    if (slot >= 0) {
        cached_handle_t *h = s_cached_handles[slot];
        size_t want = size * count;
        size_t avail = (h->cursor < h->size) ? (h->size - h->cursor) : 0;
        size_t to_copy = (want < avail) ? want : avail;
        size_t items = (size > 0) ? (to_copy / size) : 0;
        size_t bytes = items * size;
        if (bytes > 0) {
            memcpy(ptr, h->data + h->cursor, bytes);
            h->cursor += bytes;
        }
        return items;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fread(ptr, size, count, stream);
#else
    return fread(ptr, size, count, stream);
#endif
}

int fseek_soloader(FILE *stream, long offset, int origin) {
    int slot = find_cached_handle_slot(stream);
    if (slot >= 0) {
        cached_handle_t *h = s_cached_handles[slot];
        long base;
        if (origin == SEEK_SET) base = 0;
        else if (origin == SEEK_CUR) base = (long)h->cursor;
        else if (origin == SEEK_END) base = (long)h->size;
        else return -1;
        long newpos = base + offset;
        if (newpos < 0) newpos = 0;
        if ((size_t)newpos > h->size) newpos = (long)h->size;
        h->cursor = (size_t)newpos;
        return 0;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fseek(stream, offset, origin);
#else
    return fseek(stream, offset, origin);
#endif
}

long ftell_soloader(FILE *stream) {
    int slot = find_cached_handle_slot(stream);
    if (slot >= 0) return (long)s_cached_handles[slot]->cursor;
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_ftell(stream);
#else
    return ftell(stream);
#endif
}

int feof_soloader(FILE *stream) {
    int slot = find_cached_handle_slot(stream);
    if (slot >= 0) {
        cached_handle_t *h = s_cached_handles[slot];
        return (h->cursor >= h->size) ? 1 : 0;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_feof(stream);
#else
    return feof(stream);
#endif
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

// Safety nets for the sound cache: libshadowguardian.so imports these too
// (confirmed via objdump -T), and while nothing in the sounds/*.wav read
// path is expected to reach them, a crash from a mismatched handle would be
// far worse than the perf win this cache buys -- degrade gracefully instead
// of dereferencing a fake handle as a real FILE*.
int ferror_soloader(FILE *stream) {
    if (find_cached_handle_slot(stream) >= 0) return 0;
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_ferror(stream);
#else
    return ferror(stream);
#endif
}

int ungetc_soloader(int c, FILE *stream) {
    int slot = find_cached_handle_slot(stream);
    if (slot >= 0) {
        cached_handle_t *h = s_cached_handles[slot];
        if (h->cursor > 0) {
            h->cursor--;
            return c;
        }
        return -1;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_ungetc(c, stream);
#else
    return ungetc(c, stream);
#endif
}

int setvbuf_soloader(FILE *stream, char *buf, int mode, size_t size) {
    if (find_cached_handle_slot(stream) >= 0) return 0;
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_setvbuf(stream, buf, mode, size);
#else
    return setvbuf(stream, buf, mode, size);
#endif
}

int fgetpos_soloader(FILE *stream, fpos_t *pos) {
    if (find_cached_handle_slot(stream) >= 0) {
        if (pos) memset(pos, 0, sizeof(*pos));
        return 0;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fgetpos(stream, pos);
#else
    return fgetpos(stream, pos);
#endif
}

int fsetpos_soloader(FILE *stream, const fpos_t *pos) {
    if (find_cached_handle_slot(stream) >= 0) return 0;
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fsetpos(stream, pos);
#else
    return fsetpos(stream, pos);
#endif
}

FILE * freopen_soloader(const char *filename, const char *mode, FILE *stream) {
    int slot = find_cached_handle_slot(stream);
    if (slot >= 0) {
        free(s_cached_handles[slot]);
        s_cached_handles[slot] = NULL;
        return fopen_soloader(filename, mode);
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_freopen(filename, mode, stream);
#else
    return freopen(filename, mode, stream);
#endif
}

int fclose_soloader(FILE * f) {
    int slot = find_cached_handle_slot(f);
    if (slot >= 0) {
        free(s_cached_handles[slot]);
        s_cached_handles[slot] = NULL;
        l_debug("fclose(%p): 0 (cached sound handle)", f);
        return 0;
    }

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
