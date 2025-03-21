
#ifndef __FPGA_FT601_H__
#define __FPGA_FT601_H__

#include <unistd.h>
#include <stdint.h>

struct fpga_context;

struct fpga_context* fpga_open(void *pvArg, uint32_t dwFlags);
uint32_t fpga_close(struct fpga_context *ctx);
uint32_t fpga_get_chip_configuration(struct fpga_context *ctx, void *config);
uint32_t fpga_set_chip_configuration(struct fpga_context *ctx, void *config);
uint32_t fpga_read(struct fpga_context *ctx, void *data, uint32_t size, uint32_t *transferred);
uint32_t fpga_write(struct fpga_context *ctx, void *data, uint32_t size, uint32_t *transferred);

int fpga_async_init(struct fpga_context *ctx);
int fpga_async_close(struct fpga_context *ctx);
int fpga_async_read(struct fpga_context *ctx, void *data, int size);
int fpga_async_result(struct fpga_context *ctx, uint32_t *transferred);

/*
The FTDI device has 2 interfaces, with one or multiple endpoints, depending the configuration.
Interface 0:
	endpoint 0x01 : OUT BULK endpoint for Session List commands
	endpoint 0x81: IN INTERRUPT endpoint for Notification List commands
Interface 1:
	endpoint 0x02-0x05: OUT BULK endpoint for application write access
	endpoint 0x82-0x85: IN BULK endpoint for application read access

We only use interface 1 and 0x02 0x82 endspoints
*/

#define FTDI_VENDOR_ID 0x0403
#define FTDI_FT60X_PRODUCT_ID 0x601f
#define FTDI_COMMUNICATION_INTERFACE 0x00
#define FTDI_DATA_INTERFACE 0x01
#define FTDI_ENDPOINT_SESSION_OUT 0x01
#define FTDI_ENDPOINT_OUT 0x02
#define FTDI_ENDPOINT_IN 0x82

// from pcie_screamer driver

struct ft60x_ctrlreq {
	unsigned int idx;
	unsigned char pipe;
	unsigned char cmd;
	unsigned char unk1;
	unsigned char unk2;
	unsigned int len;
	unsigned int unk4;
	unsigned int unk5;
} __attribute__ ((packed));

// from ft3xx.h

//
// Chip configuration - FIFO Mode
//
enum CONFIGURATION_FIFO_MODE {
	CONFIGURATION_FIFO_MODE_245,
	CONFIGURATION_FIFO_MODE_600,
	CONFIGURATION_FIFO_MODE_COUNT,
};

//
// Chip configuration - Channel Configuration
//
enum CONFIGURATION_CHANNEL_CONFIG {
	CONFIGURATION_CHANNEL_CONFIG_4,
	CONFIGURATION_CHANNEL_CONFIG_2,
	CONFIGURATION_CHANNEL_CONFIG_1,
	CONFIGURATION_CHANNEL_CONFIG_1_OUTPIPE,
	CONFIGURATION_CHANNEL_CONFIG_1_INPIPE,
	CONFIGURATION_CHANNEL_CONFIG_COUNT,
};

//
// Chip configuration - Optional Feature Support
//
enum CONFIGURATION_OPTIONAL_FEATURE_SUPPORT {
	CONFIGURATION_OPTIONAL_FEATURE_DISABLEALL = 0,
	CONFIGURATION_OPTIONAL_FEATURE_ENABLEBATTERYCHARGING = 1,
	CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN = 2,
	CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH1 = 4,
	CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH2 = 8,
	CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH3 = 0x10,
	CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH4 = 0x20,
	CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCHALL = 0x3C,
	CONFIGURATION_OPTIONAL_FEATURE_DISABLEUNDERRUN_INCH1   = (0x1 << 6),
	CONFIGURATION_OPTIONAL_FEATURE_DISABLEUNDERRUN_INCH2   = (0x1 << 7),
	CONFIGURATION_OPTIONAL_FEATURE_DISABLEUNDERRUN_INCH3   = (0x1 << 8),
	CONFIGURATION_OPTIONAL_FEATURE_DISABLEUNDERRUN_INCH4   = (0x1 << 9),
	CONFIGURATION_OPTIONAL_FEATURE_DISABLEUNDERRUN_INCHALL = (0xF << 6),
};

struct FT_60XCONFIGURATION {
	// Device Descriptor
	short       VendorID;
	short       ProductID;

	// String Descriptors
	char        StringDescriptors[128];

	// Configuration Descriptor
	char        Reserved;
	char        PowerAttributes;
	short       PowerConsumption;

	// Data Transfer Configuration
	char        reserved;
	char        FIFOClock;
	char        FIFOMode;
	char        ChannelConfig;

	// Optional Feature Support
	short       OptionalFeatureSupport;
	char        BatteryChargingGPIOConfig;
	char        FlashEEPROMDetection;      // Read-only

	// MSIO and GPIO Configuration
	unsigned int        MSIO_Control;
	unsigned int        GPIO_Control;
};

#endif /* __FPGA_FT601_H__ */
