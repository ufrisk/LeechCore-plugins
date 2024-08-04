CC=gcc
CFLAGS  += -I. -I../includes -D LINUX -shared -fPIC -fvisibility=hidden
LDFLAGS += -g -shared
DEPS = 
OBJ = leechcore_device_qemupcileech.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

leechcore_device_qemupcileech: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) -o leechcore_device_qemupcileech.so $(LDFLAGS)
	rm *.o
	mkdir -p ../files
	mv leechcore_device_qemupcileech.so ../files/
	true

clean:
	rm -f *.o
