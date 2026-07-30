CC ?= gcc
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
	-Wno-unused-parameter -Wno-sign-compare -g
CPPFLAGS += -I../../leechcore_ft601_driver_linux -I. \
	$(shell pkg-config libusb-1.0 --cflags)
LDLIBS += -pthread

SHIM_SOURCES = \
	shim_test.c \
	fake_libusb.c \
	../../leechcore_ft601_driver_linux/fpga_libusb.c \
	../../leechcore_ft601_driver_linux/leechcore_ft601_driver_linux.c

.PHONY: all clean test

all: shim_test

shim_test: $(SHIM_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SHIM_SOURCES) $(LDLIBS)

test: shim_test
	./shim_test

clean:
	rm -f shim_test posix_test abi_smoke
