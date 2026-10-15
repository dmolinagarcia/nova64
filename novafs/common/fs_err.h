/*
 * fs_err.h — filesystem-level error codes (sheet Y4.8).
 *
 * L2 reports its own error enum, deliberately narrower than sheet Y1.15's
 * error space, and whatever binds it to an OS (the FUSE binding here, the
 * VFS on the machine) maps it once, at the boundary.
 *
 * FS_ECORRUPT is the one code with behaviour attached: it is always
 * raised through fs_corrupt(), which logs, because damage reported
 * silently looks exactly like an empty directory or a short file.
 */
#ifndef NOVAFS_FS_ERR_H
#define NOVAFS_FS_ERR_H

typedef enum {
    FS_OK = 0,
    FS_ENOENT,          /* name not found; also "end of directory"    */
    FS_ENOTDIR,         /* path component is not a directory          */
    FS_EISDIR,          /* file operation on a directory              */
    FS_EIO,             /* backing store failure                      */
    FS_ECORRUPT,        /* on-disk structure violates the format      */
    FS_ENOSPC,
    FS_EROFS,
    FS_EEXIST,
    FS_ENOTEMPTY,
    FS_ENAMETOOLONG,
    FS_EINVAL,          /* bad argument from the caller               */
    FS_ENOTSUP,         /* format or feature this driver refuses      */
    FS_ELOOP,           /* structural cycle or depth bound exceeded   */
    FS_EMFILE           /* no free open-file or directory slot        */
} fs_err_t;

const char *fs_strerror(fs_err_t e);

/* Corruption reporting. The logger receives a short static description.
 * The default logger writes to stderr, unless the library is built with
 * NOVAFS_FREESTANDING, in which case it does nothing until one is set.
 * Passing NULL restores the default. */
typedef void (*fs_log_fn)(const char *what);

void     fs_set_corrupt_logger(fs_log_fn fn);
fs_err_t fs_corrupt(const char *what);      /* logs, returns FS_ECORRUPT */

#endif /* NOVAFS_FS_ERR_H */
