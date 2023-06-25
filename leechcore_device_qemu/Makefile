CC=gcc
# -Wno-unused-variable -> unused variable in leechcore.h
CFLAGS  += -I. -I../includes -D LINUX -shared -fPIC -fvisibility=hidden -g -Wall -Werror -Wextra -Wno-unused-variable
LDFLAGS += -shared -lrt
OBJ = leechcore_device_qemu.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

leechcore_device_qemu: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) -o leechcore_device_qemu.so $(LDFLAGS)
	mkdir -p ../files
	mv leechcore_device_qemu.so ../files/

clean:
	rm -f *.o
