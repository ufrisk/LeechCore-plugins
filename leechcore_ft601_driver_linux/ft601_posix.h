#ifndef __FT601_POSIX_H__
#define __FT601_POSIX_H__

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct tdFT601_POSIX_OPS {
    ssize_t (*read_fn)(int fd, void *buffer, size_t length);
    ssize_t (*write_fn)(int fd, const void *buffer, size_t length);
    int (*close_fn)(int fd);
    uint64_t (*now_ms_fn)(void);
    void (*sleep_ms_fn)(uint32_t milliseconds);
} FT601_POSIX_OPS;

int ft601_posix_read_deadline(
    const FT601_POSIX_OPS *ops,
    int fd,
    void *buffer,
    size_t length,
    uint32_t timeout_ms,
    size_t *transferred,
    int *read_pending
);

int ft601_posix_write_deadline(
    const FT601_POSIX_OPS *ops,
    int fd,
    const void *buffer,
    size_t length,
    uint32_t timeout_ms,
    size_t *transferred
);

#endif
