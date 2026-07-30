#include "../../leechcore_ft601_driver_linux/ft601_posix.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct tdSCRIPT_STEP {
    ssize_t result;
    int error;
} SCRIPT_STEP;

static SCRIPT_STEP g_read_steps[8];
static SCRIPT_STEP g_write_steps[8];
static size_t g_read_step_count;
static size_t g_write_step_count;
static size_t g_read_call_count;
static size_t g_write_call_count;
static uint64_t g_now_ms;
static unsigned int g_assertions;
static unsigned int g_failures;

#define ASSERT_EQ(actual, expected)                                      \
    do {                                                                 \
        uint64_t actual_value = (uint64_t)(actual);                       \
        uint64_t expected_value = (uint64_t)(expected);                   \
        g_assertions++;                                                   \
        if(actual_value != expected_value) {                              \
            fprintf(                                                     \
                stderr,                                                  \
                "%s:%d: expected %llu, got %llu\n",                      \
                __FILE__,                                                \
                __LINE__,                                                \
                (unsigned long long)expected_value,                       \
                (unsigned long long)actual_value);                        \
            g_failures++;                                                 \
        }                                                                \
    } while(0)

static void reset_script(void)
{
    memset(g_read_steps, 0, sizeof(g_read_steps));
    memset(g_write_steps, 0, sizeof(g_write_steps));
    g_read_step_count = 0;
    g_write_step_count = 0;
    g_read_call_count = 0;
    g_write_call_count = 0;
    g_now_ms = 100;
}

static ssize_t scripted_read(int fd, void *buffer, size_t length)
{
    SCRIPT_STEP step;
    (void)fd;
    (void)buffer;
    (void)length;
    if(g_read_call_count < g_read_step_count) {
        step = g_read_steps[g_read_call_count];
    } else {
        step.result = -1;
        step.error = EAGAIN;
    }
    g_read_call_count++;
    errno = step.error;
    return step.result;
}

static ssize_t scripted_write(
    int fd,
    const void *buffer,
    size_t length
)
{
    SCRIPT_STEP step;
    (void)fd;
    (void)buffer;
    (void)length;
    if(g_write_call_count < g_write_step_count) {
        step = g_write_steps[g_write_call_count];
    } else {
        step.result = -1;
        step.error = EAGAIN;
    }
    g_write_call_count++;
    errno = step.error;
    return step.result;
}

static uint64_t scripted_now_ms(void)
{
    return g_now_ms;
}

static void scripted_sleep_ms(uint32_t milliseconds)
{
    g_now_ms += milliseconds;
}

static const FT601_POSIX_OPS g_ops = {
    .read_fn = scripted_read,
    .write_fn = scripted_write,
    .close_fn = NULL,
    .now_ms_fn = scripted_now_ms,
    .sleep_ms_fn = scripted_sleep_ms
};

static void test_read_retries_pending_results(void)
{
    unsigned char buffer[8] = { 0 };
    size_t transferred = 0;
    int read_pending = 0;
    reset_script();
    g_read_steps[0] = (SCRIPT_STEP){ -1, EAGAIN };
    g_read_steps[1] = (SCRIPT_STEP){ -1, EWOULDBLOCK };
    g_read_steps[2] = (SCRIPT_STEP){ 4, 0 };
    g_read_step_count = 3;
    ASSERT_EQ(
        ft601_posix_read_deadline(
            &g_ops,
            7,
            buffer,
            sizeof(buffer),
            10,
            &transferred,
            &read_pending),
        0);
    ASSERT_EQ(transferred, 4);
    ASSERT_EQ(read_pending, 0);
    ASSERT_EQ(g_read_call_count, 3);
    ASSERT_EQ(g_now_ms, 102);
}

static void test_read_timeout_marks_pending(void)
{
    unsigned char buffer[8] = { 0 };
    size_t transferred = 99;
    int read_pending = 0;
    reset_script();
    errno = 0;
    ASSERT_EQ(
        ft601_posix_read_deadline(
            &g_ops,
            7,
            buffer,
            sizeof(buffer),
            3,
            &transferred,
            &read_pending),
        -1);
    ASSERT_EQ(errno, ETIMEDOUT);
    ASSERT_EQ(transferred, 0);
    ASSERT_EQ(read_pending, 1);
    ASSERT_EQ(g_read_call_count, 3);
    ASSERT_EQ(g_now_ms, 103);
}

static void test_write_retries_and_completes(void)
{
    const unsigned char buffer[5] = { 1, 2, 3, 4, 5 };
    size_t transferred = 0;
    reset_script();
    g_write_steps[0] = (SCRIPT_STEP){ -1, EAGAIN };
    g_write_steps[1] = (SCRIPT_STEP){ 3, 0 };
    g_write_steps[2] = (SCRIPT_STEP){ 2, 0 };
    g_write_step_count = 3;
    ASSERT_EQ(
        ft601_posix_write_deadline(
            &g_ops,
            8,
            buffer,
            sizeof(buffer),
            10,
            &transferred),
        0);
    ASSERT_EQ(transferred, sizeof(buffer));
    ASSERT_EQ(g_write_call_count, 3);
    ASSERT_EQ(g_now_ms, 101);
}

static void test_write_timeout_is_bounded(void)
{
    const unsigned char buffer[5] = { 1, 2, 3, 4, 5 };
    size_t transferred = 99;
    reset_script();
    errno = 0;
    ASSERT_EQ(
        ft601_posix_write_deadline(
            &g_ops,
            8,
            buffer,
            sizeof(buffer),
            2,
            &transferred),
        -1);
    ASSERT_EQ(errno, ETIMEDOUT);
    ASSERT_EQ(transferred, 0);
    ASSERT_EQ(g_write_call_count, 2);
    ASSERT_EQ(g_now_ms, 102);
}

static void test_zero_timeout_is_rejected(void)
{
    unsigned char buffer[1] = { 0 };
    size_t transferred = 99;
    int read_pending = 0;
    reset_script();
    errno = 0;
    ASSERT_EQ(
        ft601_posix_read_deadline(
            &g_ops,
            7,
            buffer,
            sizeof(buffer),
            0,
            &transferred,
            &read_pending),
        -1);
    ASSERT_EQ(errno, EINVAL);
    ASSERT_EQ(g_read_call_count, 0);
    ASSERT_EQ(
        ft601_posix_write_deadline(
            &g_ops,
            8,
            buffer,
            sizeof(buffer),
            0,
            &transferred),
        -1);
    ASSERT_EQ(errno, EINVAL);
    ASSERT_EQ(g_write_call_count, 0);
}

int main(void)
{
    test_read_retries_pending_results();
    test_read_timeout_marks_pending();
    test_write_retries_and_completes();
    test_write_timeout_is_bounded();
    test_zero_timeout_is_rejected();
    if(g_failures) {
        fprintf(
            stderr,
            "FAILED: %u of %u assertions\n",
            g_failures,
            g_assertions);
        return 1;
    }
    printf("PASS: %u assertions\n", g_assertions);
    return 0;
}
