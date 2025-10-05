CC=gcc
CFLAGS  += -I. -D LINUX -shared -fPIC -fvisibility=hidden `pkg-config libusb-1.0 --libs --cflags`
CFLAGS  += -fPIE -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2 -O1
CFLAGS  += -Wall -Wno-format-truncation -Wno-enum-compare -Wno-pointer-sign -Wno-multichar -Wno-unused-variable -Wno-unused-value
CFLAGS  += -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
LDFLAGS += -Wl,-rpath,'$$ORIGIN' -g -ldl -shared -L. -lm -Wl,-z,noexecstack
DEPS = leechcore_ft601_driver_linux.h
OBJ = fpga_ft601.o leechcore_ft601_driver_linux.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

leechcore_ft601_driver_linux: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) -o leechcore_ft601_driver_linux.so $(LDFLAGS)
	rm *.o
	mkdir -p ../files
	mv leechcore_ft601_driver_linux.so ../files/
	true

clean:
	rm -f *.o
