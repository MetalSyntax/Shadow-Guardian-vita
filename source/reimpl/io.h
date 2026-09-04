/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  io.h
 * @brief Wrappers and implementations for some of the IO functions.
 */

#ifndef SOLOADER_IO_H
#define SOLOADER_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>
#include <sys/dirent.h>
#include <sys/syslimits.h>
#include <sys/fcntl.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef DT_DIR
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#endif

typedef struct __attribute__((__packed__)) stat64_bionic {
    unsigned long long st_dev;
    unsigned char __pad0[4];
    unsigned long __st_ino;
    unsigned int st_mode;
    // Bionic's nlink_t/uid_t/gid_t are all 4-byte unsigned int; newlib's are
    // 2-byte unsigned short (see arm-vita-eabi/include/sys/_types.h). Using
    // the newlib typedefs here made this struct 6 bytes shorter than what the
    // compiled .so's machine code expects, shifting every field from st_rdev
    // onward (including st_size and st_blksize) by 6 bytes relative to the
    // offsets the game reads at -- confirmed via offsetof() on both layouts.
    // This is what was actually behind the "mmap(..., garbage tiny length,
    // ...)" texture-load failures that survived the st_blksize=st_size fix in
    // pig::stream::MMap() (source/reimpl/mem.c): that fix wrote the real file
    // size into the field at *our* wrong offset, which the game then read
    // from a completely different (mis-shifted) byte range.
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned long long st_rdev;
    // NOTE: no __pad3 padding here, unlike some published bionic stat64
    // headers -- disassembling the real compiled binary's MMap()/MUnmap()
    // (pig::stream, addresses 0x2ffb30/0x2ffc40 in libshadowguardian.so)
    // shows both read the mmap()/munmap() length as a 32-bit word at
    // *exactly* offset 0x30 (48) from the struct base passed to fstat().
    // st_rdev already ends at offset 32 (a multiple of 8), so no filler is
    // needed to keep st_size 8-byte aligned, and adding one anyway (as an
    // earlier fix here did, copying a generic bionic reference header)
    // pushed st_size/st_blksize 4 bytes past where the game's actual
    // compiled code reads them -- so every mmap()-based texture/asset load
    // read a near-zero garbage length (2, 10, 0, ... bytes observed in
    // game_log_1788552823938.txt for MC_ALEX_DF.tga, TT_BasicSoldier_DF.tga,
    // and ~100+ other character/world textures) despite the st_blksize =
    // st_size hack in stat_newlib_to_bionic() being logically correct -- it
    // was just writing the right value 4 bytes away from where the game
    // reads it back. Removing this pad makes st_size start at offset 40 and
    // st_blksize land at offset 48 (0x30), matching the disassembly exactly.
    long long st_size;
    unsigned long st_blksize;
    unsigned long long st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    unsigned long long st_ino;
} stat64_bionic;

// Locks in the offset confirmed by disassembling libshadowguardian.so's
// MMap()/MUnmap() (0x2ffb30/0x2ffc40): both read the mmap()/munmap() length
// as a 32-bit word at exactly offset 0x30 from the stat buffer base. If this
// ever fires, some field above changed size/order and every mmap()-based
// texture/asset load will silently go back to reading a garbage length.
#ifndef __cplusplus
_Static_assert(offsetof(stat64_bionic, st_blksize) == 0x30,
               "stat64_bionic.st_blksize must sit at offset 0x30 -- "
               "see MMap()/MUnmap() in libshadowguardian.so");
#endif

typedef struct __attribute__((__packed__)) dirent64_bionic {
    int16_t d_ino; // 2 bytes // offset 0x0
    int64_t d_off; // 8 bytes // offset 0x2
    uint64_t d_reclen; // 8 bytes // 0xA
    unsigned char d_type; // 1 byte // offset 0x12
    char d_name[256]; // 256 bytes // offset 0x13
} dirent64_bionic;

int open_soloader(const char * path, int oflag, ...);

FILE * fopen_soloader(const char * filename, const char * mode);

DIR *opendir_soloader(char *name);

int stat_soloader(const char * path, stat64_bionic * buf);

int fstat_soloader(int fd, stat64_bionic * buf);

struct dirent64_bionic * readdir_soloader(DIR *dir);

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result);

int close_soloader(int fd);

int fclose_soloader(FILE *f);

int closedir_soloader(DIR *dir);

int fcntl_soloader(int fd, int cmd, ...);

int ioctl_soloader(int fd, int request, ... /* arg */);

int fsync_soloader(int fd);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_IO_H
