CC=gcc
CFLAGS  += -I. -D LINUX -shared -fPIC -fvisibility=hidden `pkg-config libusb-1.0 --libs --cflags`
LDFLAGS += -g -shared
DEPS = leechcore_ft601_driver_linux.h
OBJ = fpga_libusb.o leechcore_ft601_driver_linux.o

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
