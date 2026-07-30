#include "ft601_posix.h"

#include <errno.h>
#include <stdint.h>

static int ft601_posix_validate(
    const FT601_POSIX_OPS *ops,
    const void *buffer,
    size_t length,
    uint32_t timeout_ms,
    size_t *transferred
)
{
    if(!ops || !buffer || !length || !timeout_ms || !transferred ||
       !ops->now_ms_fn || !ops->sleep_ms_fn) {
        errno = EINVAL;
        return -1;
    }
    *transferred = 0;
    return 0;
}

static uint64_t ft601_posix_deadline(
    uint64_t start_ms,
    uint32_t timeout_ms
)
{
    if(UINT64_MAX - start_ms < timeout_ms) {
        return UINT64_MAX;
    }
    return start_ms + timeout_ms;
}

int ft601_posix_read_deadline(
    const FT601_POSIX_OPS *ops,
    int fd,
    void *buffer,
    size_t length,
    uint32_t timeout_ms,
    size_t *transferred,
    int *read_pending
)
{
    int pending = 0;
    ssize_t result;
    uint64_t deadline;
    if(!read_pending ||
       ft601_posix_validate(
           ops,
           buffer,
           length,
           timeout_ms,
           transferred) ||
       !ops->read_fn) {
        errno = EINVAL;
        return -1;
    }
    *read_pending = 0;
    deadline = ft601_posix_deadline(ops->now_ms_fn(), timeout_ms);
    for(;;) {
        if(ops->now_ms_fn() >= deadline) {
            *read_pending = pending;
            errno = ETIMEDOUT;
            return -1;
        }
        result = ops->read_fn(fd, buffer, length);
        if(result >= 0) {
            *transferred = (size_t)result;
            return 0;
        }
        if(errno == EINTR) {
            continue;
        }
        if(errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
        pending = 1;
        ops->sleep_ms_fn(1);
    }
}

int ft601_posix_write_deadline(
    const FT601_POSIX_OPS *ops,
    int fd,
    const void *buffer,
    size_t length,
    uint32_t timeout_ms,
    size_t *transferred
)
{
    const unsigned char *cursor = buffer;
    ssize_t result;
    uint64_t deadline;
    if(ft601_posix_validate(
           ops,
           buffer,
           length,
           timeout_ms,
           transferred) ||
       !ops->write_fn) {
        errno = EINVAL;
        return -1;
    }
    deadline = ft601_posix_deadline(ops->now_ms_fn(), timeout_ms);
    while(*transferred < length) {
        if(ops->now_ms_fn() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        result = ops->write_fn(
            fd,
            cursor + *transferred,
            length - *transferred);
        if(result > 0) {
            *transferred += (size_t)result;
            continue;
        }
        if(result == 0) {
            errno = EIO;
            return -1;
        }
        if(errno == EINTR) {
            continue;
        }
        if(errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
        ops->sleep_ms_fn(1);
    }
    return 0;
}
