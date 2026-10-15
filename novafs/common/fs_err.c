/*
 * fs_err.c — error strings and the corruption logger.
 */
#include "fs_err.h"

#ifndef NOVAFS_FREESTANDING
#include <stdio.h>

static void default_logger(const char *what)
{
    fprintf(stderr, "novafs: corrupt filesystem: %s\n", what);
}
#define DEFAULT_LOGGER default_logger
#else
#define DEFAULT_LOGGER ((fs_log_fn)0)
#endif

static fs_log_fn corrupt_logger = DEFAULT_LOGGER;

void fs_set_corrupt_logger(fs_log_fn fn)
{
    corrupt_logger = fn ? fn : DEFAULT_LOGGER;
}

fs_err_t fs_corrupt(const char *what)
{
    if (corrupt_logger)
        corrupt_logger(what);
    return FS_ECORRUPT;
}

const char *fs_strerror(fs_err_t e)
{
    switch (e) {
    case FS_OK:           return "success";
    case FS_ENOENT:       return "no such file or directory";
    case FS_ENOTDIR:      return "not a directory";
    case FS_EISDIR:       return "is a directory";
    case FS_EIO:          return "input/output error";
    case FS_ECORRUPT:     return "filesystem is corrupt";
    case FS_ENOSPC:       return "no space left on device";
    case FS_EROFS:        return "read-only filesystem";
    case FS_EEXIST:       return "file exists";
    case FS_ENOTEMPTY:    return "directory not empty";
    case FS_ENAMETOOLONG: return "name too long";
    case FS_EINVAL:       return "invalid argument";
    case FS_ENOTSUP:      return "not supported";
    case FS_ELOOP:        return "too many levels";
    case FS_EMFILE:       return "too many open files";
    }
    return "unknown error";
}
