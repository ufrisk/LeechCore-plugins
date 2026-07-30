#ifndef __FAKE_LIBUSB_H__
#define __FAKE_LIBUSB_H__

typedef enum tdFAKE_ASYNC_OUTCOME {
    FAKE_ASYNC_PENDING = 0,
    FAKE_ASYNC_COMPLETE,
    FAKE_ASYNC_ERROR
} FAKE_ASYNC_OUTCOME;

void fake_libusb_reset(void);
void fake_libusb_set_sync_read(int status, int transferred, unsigned int delay_ms);
void fake_libusb_set_async_outcome(FAKE_ASYNC_OUTCOME outcome, int transferred);
unsigned int fake_libusb_last_sync_timeout(void);
unsigned int fake_libusb_cancel_count(void);
unsigned int fake_libusb_set_config_count(void);
unsigned int fake_libusb_free_device_list_count(void);
unsigned int fake_libusb_release_interface_count(void);
unsigned int fake_libusb_close_count(void);
unsigned int fake_libusb_exit_count(void);

#endif
