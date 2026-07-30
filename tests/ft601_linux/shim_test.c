#include "fake_libusb.h"
#include "../../leechcore_ft601_driver_linux/leechcore_ft601_driver_linux.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FT_OK               0
#define FT_INVALID_PARAMETER 6
#define FT_TIMEOUT          19
#define FT_IO_PENDING       24
#define FT_IO_INCOMPLETE    25
#define FT_OTHER_ERROR      32
#define FT_OPEN_BY_INDEX    0x10

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

#define ASSERT_NOT_NULL(value)                                           \
    do {                                                                 \
        g_assertions++;                                                   \
        if(!(value)) {                                                    \
            fprintf(stderr, "%s:%d: expected non-NULL\n", __FILE__, __LINE__); \
            g_failures++;                                                 \
        }                                                                \
    } while(0)

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return
        ((uint64_t)now.tv_sec * 1000) +
        ((uint64_t)now.tv_nsec / 1000000);
}

static void close_overlapped(
    struct ft_handle *handle,
    void *overlapped
)
{
    uint32_t transferred = 0;
    FT_AbortPipe(handle, 0x82);
    FT_GetOverlappedResult(handle, overlapped, &transferred, 0);
    FT_ReleaseOverlapped(handle, overlapped);
    FT_Close(handle);
}

static struct ft_handle *open_test_handle(void)
{
    struct ft_handle *handle = NULL;
    ASSERT_EQ(FT_Create(NULL, FT_OPEN_BY_INDEX, &handle), FT_OK);
    ASSERT_NOT_NULL(handle);
    return handle;
}

static void test_open_does_not_write_configuration(void)
{
    struct ft_handle *handle;
    fake_libusb_reset();
    handle = open_test_handle();
    ASSERT_EQ(fake_libusb_set_config_count(), 0);
    if(handle) {
        ASSERT_EQ(FT_Close(handle), FT_OK);
    }
}

static void test_full_configuration_write_is_success(void)
{
    unsigned char config[152];
    struct ft_handle *handle;
    memset(config, 0, sizeof(config));
    fake_libusb_reset();
    handle = open_test_handle();
    if(handle) {
        ASSERT_EQ(FT_SetChipConfiguration(handle, config), FT_OK);
        ASSERT_EQ(fake_libusb_set_config_count(), 1);
        ASSERT_EQ(FT_Close(handle), FT_OK);
    }
}

static void test_close_releases_libusb_resources(void)
{
    struct ft_handle *handle;
    fake_libusb_reset();
    handle = open_test_handle();
    if(handle) {
        ASSERT_EQ(FT_Close(handle), FT_OK);
        ASSERT_EQ(fake_libusb_free_device_list_count(), 1);
        ASSERT_EQ(fake_libusb_release_interface_count(), 2);
        ASSERT_EQ(fake_libusb_close_count(), 1);
        ASSERT_EQ(fake_libusb_exit_count(), 1);
    }
}

static void test_pipe_timeouts_validate_and_reach_libusb(void)
{
    unsigned char buffer[32] = { 0 };
    uint32_t transferred = 0;
    struct ft_handle *handle;
    fake_libusb_reset();
    handle = open_test_handle();
    if(!handle) {
        return;
    }
    ASSERT_EQ(FT_SetPipeTimeout(handle, 0x82, 123), FT_OK);
    ASSERT_EQ(FT_SetPipeTimeout(handle, 0x02, 456), FT_OK);
    ASSERT_EQ(
        FT_SetPipeTimeout(handle, 0x81, 123),
        FT_INVALID_PARAMETER);
    ASSERT_EQ(
        FT_SetPipeTimeout(handle, 0x03, 456),
        FT_INVALID_PARAMETER);
    ASSERT_EQ(
        FT_SetPipeTimeout(handle, 0x82, 0),
        FT_INVALID_PARAMETER);
    ASSERT_EQ(
        FT_ReadPipe(
            handle,
            0x82,
            buffer,
            sizeof(buffer),
            &transferred,
            NULL),
        FT_OK);
    ASSERT_EQ(fake_libusb_last_sync_timeout(), 123);
    ASSERT_EQ(
        FT_WritePipe(
            handle,
            0x02,
            buffer,
            sizeof(buffer),
            &transferred,
            NULL),
        FT_OK);
    ASSERT_EQ(fake_libusb_last_sync_timeout(), 456);
    ASSERT_EQ(FT_Close(handle), FT_OK);
}

static void test_async_read_uses_configured_rx_timeout(void)
{
    unsigned char buffer[64] = { 0 };
    unsigned char overlapped[64] = { 0 };
    uint32_t transferred = 0;
    struct ft_handle *handle;
    fake_libusb_reset();
    handle = open_test_handle();
    if(!handle) {
        return;
    }
    ASSERT_EQ(FT_InitializeOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(FT_SetPipeTimeout(handle, 0x82, 123), FT_OK);
    fake_libusb_set_async_outcome(FAKE_ASYNC_PENDING, 0);
    ASSERT_EQ(
        FT_ReadPipe(
            handle,
            0x82,
            buffer,
            sizeof(buffer),
            &transferred,
            overlapped),
        FT_IO_PENDING);
    ASSERT_EQ(fake_libusb_last_async_timeout(), 123);
    close_overlapped(handle, overlapped);
}

static void test_libusb_timeout_is_not_flattened(void)
{
    unsigned char buffer[32] = { 0 };
    uint32_t transferred = 99;
    struct ft_handle *handle;
    fake_libusb_reset();
    handle = open_test_handle();
    if(!handle) {
        return;
    }
    ASSERT_EQ(FT_SetPipeTimeout(handle, 0x82, 77), FT_OK);
    fake_libusb_set_sync_read(LIBUSB_ERROR_TIMEOUT, 0, 0);
    ASSERT_EQ(
        FT_ReadPipe(
            handle,
            0x82,
            buffer,
            sizeof(buffer),
            &transferred,
            NULL),
        FT_TIMEOUT);
    ASSERT_EQ(transferred, 0);
    ASSERT_EQ(fake_libusb_last_sync_timeout(), 77);
    ASSERT_EQ(FT_Close(handle), FT_OK);
}

static void test_nonblocking_result_returns_while_read_is_pending(void)
{
    unsigned char buffer[64] = { 0 };
    unsigned char overlapped[64] = { 0 };
    uint32_t transferred = 0;
    uint32_t status;
    uint64_t start;
    uint64_t elapsed;
    struct ft_handle *handle;
    fake_libusb_reset();
    fake_libusb_set_sync_read(LIBUSB_SUCCESS, sizeof(buffer), 250);
    handle = open_test_handle();
    if(!handle) {
        return;
    }
    ASSERT_EQ(FT_InitializeOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(
        FT_ReadPipe(
            handle,
            0x82,
            buffer,
            sizeof(buffer),
            &transferred,
            overlapped),
        FT_IO_PENDING);
    start = monotonic_ms();
    status = FT_GetOverlappedResult(
        handle,
        overlapped,
        &transferred,
        0);
    elapsed = monotonic_ms() - start;
    ASSERT_EQ(status, FT_IO_INCOMPLETE);
    g_assertions++;
    if(elapsed >= 50) {
        fprintf(
            stderr,
            "%s:%d: nonblocking result waited %llu ms\n",
            __FILE__,
            __LINE__,
            (unsigned long long)elapsed);
        g_failures++;
    }
    close_overlapped(handle, overlapped);
}

static void test_completed_async_read_returns_exact_length(void)
{
    unsigned char buffer[128] = { 0 };
    unsigned char overlapped[64] = { 0 };
    uint32_t transferred = 0;
    struct ft_handle *handle;
    fake_libusb_reset();
    fake_libusb_set_async_outcome(FAKE_ASYNC_PENDING, 0);
    handle = open_test_handle();
    if(!handle) {
        return;
    }
    ASSERT_EQ(FT_InitializeOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(
        FT_ReadPipe(
            handle,
            0x82,
            buffer,
            sizeof(buffer),
            &transferred,
            overlapped),
        FT_IO_PENDING);
    ASSERT_EQ(
        FT_GetOverlappedResult(
            handle,
            overlapped,
            &transferred,
            0),
        FT_IO_INCOMPLETE);
    fake_libusb_set_async_outcome(FAKE_ASYNC_COMPLETE, 84);
    ASSERT_EQ(
        FT_GetOverlappedResult(
            handle,
            overlapped,
            &transferred,
            0),
        FT_OK);
    ASSERT_EQ(transferred, 84);
    ASSERT_EQ(FT_ReleaseOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(FT_Close(handle), FT_OK);
}

static void test_abort_cancels_active_libusb_read(void)
{
    unsigned char buffer[64] = { 0 };
    unsigned char overlapped[64] = { 0 };
    uint32_t transferred = 0;
    struct ft_handle *handle;
    fake_libusb_reset();
    fake_libusb_set_async_outcome(FAKE_ASYNC_PENDING, 0);
    handle = open_test_handle();
    if(!handle) {
        return;
    }
    ASSERT_EQ(FT_InitializeOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(
        FT_ReadPipe(
            handle,
            0x82,
            buffer,
            sizeof(buffer),
            &transferred,
            overlapped),
        FT_IO_PENDING);
    ASSERT_EQ(FT_AbortPipe(handle, 0x82), FT_OK);
    ASSERT_EQ(fake_libusb_cancel_count(), 1);
    ASSERT_EQ(
        FT_GetOverlappedResult(
            handle,
            overlapped,
            &transferred,
            0),
        FT_OK);
    ASSERT_EQ(transferred, 0);
    ASSERT_EQ(FT_ReleaseOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(FT_Close(handle), FT_OK);
}

static void test_release_rejects_submitted_transfer(void)
{
    unsigned char buffer[64] = { 0 };
    unsigned char overlapped[64] = { 0 };
    uint32_t transferred = 0;
    struct ft_handle *handle;
    fake_libusb_reset();
    fake_libusb_set_sync_read(LIBUSB_SUCCESS, sizeof(buffer), 100);
    fake_libusb_set_async_outcome(FAKE_ASYNC_PENDING, 0);
    handle = open_test_handle();
    if(!handle) {
        return;
    }
    ASSERT_EQ(FT_InitializeOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(
        FT_ReadPipe(
            handle,
            0x82,
            buffer,
            sizeof(buffer),
            &transferred,
            overlapped),
        FT_IO_PENDING);
    ASSERT_EQ(
        FT_ReleaseOverlapped(handle, overlapped),
        FT_IO_PENDING);
    close_overlapped(handle, overlapped);
}

static void test_interrupted_event_poll_remains_pending(void)
{
    unsigned char buffer[64] = { 0 };
    unsigned char overlapped[64] = { 0 };
    uint32_t transferred = 0;
    struct ft_handle *handle;
    fake_libusb_reset();
    fake_libusb_set_async_outcome(FAKE_ASYNC_PENDING, 0);
    handle = open_test_handle();
    if(!handle) {
        return;
    }
    ASSERT_EQ(FT_InitializeOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(
        FT_ReadPipe(
            handle,
            0x82,
            buffer,
            sizeof(buffer),
            &transferred,
            overlapped),
        FT_IO_PENDING);
    fake_libusb_set_event_status(LIBUSB_ERROR_INTERRUPTED);
    ASSERT_EQ(
        FT_GetOverlappedResult(
            handle,
            overlapped,
            &transferred,
            0),
        FT_IO_INCOMPLETE);
    fake_libusb_set_event_status(LIBUSB_SUCCESS);
    fake_libusb_set_async_outcome(FAKE_ASYNC_COMPLETE, 12);
    ASSERT_EQ(
        FT_GetOverlappedResult(
            handle,
            overlapped,
            &transferred,
            0),
        FT_OK);
    ASSERT_EQ(transferred, 12);
    ASSERT_EQ(FT_ReleaseOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(FT_Close(handle), FT_OK);
}

static void test_close_cancels_submitted_transfer(void)
{
    unsigned char buffer[64] = { 0 };
    unsigned char overlapped[64] = { 0 };
    uint32_t transferred = 0;
    struct ft_handle *handle;
    fake_libusb_reset();
    fake_libusb_set_async_outcome(FAKE_ASYNC_PENDING, 0);
    handle = open_test_handle();
    if(!handle) {
        return;
    }
    ASSERT_EQ(FT_InitializeOverlapped(handle, overlapped), FT_OK);
    ASSERT_EQ(
        FT_ReadPipe(
            handle,
            0x82,
            buffer,
            sizeof(buffer),
            &transferred,
            overlapped),
        FT_IO_PENDING);
    ASSERT_EQ(FT_Close(handle), FT_OK);
    ASSERT_EQ(fake_libusb_cancel_count(), 1);
    ASSERT_EQ(fake_libusb_release_interface_count(), 2);
    ASSERT_EQ(fake_libusb_close_count(), 1);
    ASSERT_EQ(fake_libusb_exit_count(), 1);
}

int main(void)
{
    test_open_does_not_write_configuration();
    test_full_configuration_write_is_success();
    test_close_releases_libusb_resources();
    test_pipe_timeouts_validate_and_reach_libusb();
    test_async_read_uses_configured_rx_timeout();
    test_libusb_timeout_is_not_flattened();
    test_nonblocking_result_returns_while_read_is_pending();
    test_completed_async_read_returns_exact_length();
    test_abort_cancels_active_libusb_read();
    test_release_rejects_submitted_transfer();
    test_interrupted_event_poll_remains_pending();
    test_close_cancels_submitted_transfer();
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
