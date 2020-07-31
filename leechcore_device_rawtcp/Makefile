CC=gcc
CFLAGS  += -I. -I../includes -D LINUX -shared -fPIC -fvisibility=hidden
LDFLAGS += -g -shared
DEPS = 
OBJ = oscompatibility.o leechcore_device_rawtcp.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

leechcore_device_rawtcp: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) -o leechcore_device_rawtcp.so $(LDFLAGS)
	rm *.o
	mkdir -p ../files
	mv leechcore_device_rawtcp.so ../files/
	true

clean:
	rm -f *.o
