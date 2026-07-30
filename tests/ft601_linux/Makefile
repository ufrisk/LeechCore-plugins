CC ?= gcc
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
	-Wno-unused-parameter -Wno-sign-compare -g \
	-fsanitize=address,undefined
CPPFLAGS += -I../../leechcore_ft601_driver_linux -I. \
	$(shell pkg-config libusb-1.0 --cflags)
LDLIBS += -pthread -fsanitize=address,undefined

SHIM_SOURCES = \
	shim_test.c \
	fake_libusb.c \
	../../leechcore_ft601_driver_linux/ft601_posix.c \
	../../leechcore_ft601_driver_linux/fpga_libusb.c \
	../../leechcore_ft601_driver_linux/leechcore_ft601_driver_linux.c

.PHONY: all clean test abi-smoke

all: shim_test posix_test

shim_test: $(SHIM_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SHIM_SOURCES) $(LDLIBS)

posix_test: posix_test.c \
	../../leechcore_ft601_driver_linux/ft601_posix.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDLIBS)

abi_smoke: abi_smoke.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -ldl

abi-smoke: abi_smoke
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./abi_smoke

test: shim_test posix_test
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./shim_test
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./posix_test

clean:
	rm -f shim_test posix_test abi_smoke
