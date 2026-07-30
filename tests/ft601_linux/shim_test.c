#include "fake_libusb.h"
#include "../../leechcore_ft601_driver_linux/leechcore_ft601_driver_linux.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FT_OK               0
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

int main(void)
{
    test_open_does_not_write_configuration();
    test_full_configuration_write_is_success();
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
