CC=gcc
# -Wno-unused-variable -> unused variable in leechcore.h
CFLAGS  += -I. -I../includes -D LINUX -shared -fPIC -lrt -l:leechcore.so -L. -lm -fvisibility=hidden -g -Wall -Werror -Wextra -Wno-unused-variable
LDFLAGS += -Wl,-rpath,'$$ORIGIN' -g -ldl -shared
OBJ = leechcore_device_qemu.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

leechcore_device_qemu: $(OBJ)
	cp ../files/leechcore.so . || cp ../../LeechCore*/files/leechcore.so . || true
	$(CC) -o $@ $^ $(CFLAGS) -o leechcore_device_qemu.so $(LDFLAGS)
	mkdir -p ../files
	mv leechcore_device_qemu.so ../files/
	rm -f *.o || true
	rm -f *.so || true
	true


clean:
	rm -f *.o
