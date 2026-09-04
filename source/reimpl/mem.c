/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/mem.h"
#include "utils/logger.h"

#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <psp2/kernel/clib.h>

void *sceClibMemclr(void *dst, size_t len) {
    return sceClibMemset(dst, 0, len);
}

// Not a real mmap: since we can't map pages directly to a file on the Vita's
// filesystem, this reads the requested range into a heap buffer instead and
// hands that back. Good enough for the read-only, load-once-then-munmap
// usage pattern these ports rely on.
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offs) {
    l_warn("mmap(%p, %u, %i, %i, %i, %li)", addr, (unsigned) length, prot, flags, fd, offs);

    if (length <= 0 || fd < 0) {
        return MAP_FAILED;
    }

    void *ret = malloc(length);
    if (!ret) {
        return MAP_FAILED;
    }

    off_t saved_pos = lseek(fd, 0, SEEK_CUR);
    lseek(fd, offs, SEEK_SET);

    size_t total = 0;
    while (total < length) {
        ssize_t n = read(fd, (char *) ret + total, length - total);
        if (n <= 0) break;
        total += (size_t) n;
    }
    if (total < length) {
        memset((char *) ret + total, 0, length - total);
    }

    lseek(fd, saved_pos, SEEK_SET);

    return ret;
}

int munmap(void *addr, size_t length) {
    if (addr) free(addr);
    return 0;
}
