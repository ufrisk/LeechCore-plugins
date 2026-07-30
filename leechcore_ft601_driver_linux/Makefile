CC ?= gcc

CPPFLAGS += -I. -D_GNU_SOURCE -D LINUX \
	$(shell pkg-config libusb-1.0 --cflags)
CFLAGS += -std=c11 -fPIC -fvisibility=hidden \
	-Wall -Wextra -Werror
LDFLAGS += -shared
LDLIBS += $(shell pkg-config libusb-1.0 --libs) -pthread

DEPS = \
	leechcore_ft601_driver_linux.h \
	fpga_libusb.h \
	ft601_posix.h
OBJ = \
	fpga_libusb.o \
	ft601_posix.o \
	leechcore_ft601_driver_linux.o
TARGET = ../files/leechcore_ft601_driver_linux.so

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c $(DEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)
