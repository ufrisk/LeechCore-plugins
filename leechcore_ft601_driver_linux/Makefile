CC=gcc
CFLAGS  += -I. -shared -fPIC -fvisibility=hidden `pkg-config libusb-1.0 --libs --cflags`

ifeq ($(shell uname), Linux)
CFLAGS  += -D LINUX
LIB = leechcore_ft601_driver_linux.so
endif
ifeq ($(shell uname), Darwin)
LIB = leechcore_ft601_driver_linux.dylib
CFLAGS  += -D MACOS -install_name @loader_path/$(LIB)
endif


LDFLAGS += -g -shared
DEPS = leechcore_ft601_driver_linux.h
OBJ = fpga_libusb.o leechcore_ft601_driver_linux.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

leechcore_ft601_driver_linux: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) -o $(LIB) $(LDFLAGS)
	rm *.o
	mkdir -p ../files
	mv $(LIB) ../files/
	true

clean:
	rm -f *.o
