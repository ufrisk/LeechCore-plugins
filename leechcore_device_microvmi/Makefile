CC=gcc
# -Wno-unused-variable -> unused variable in leechcore.h
CFLAGS  += -I. -I../includes -D LINUX -shared -fPIC -fvisibility=hidden -g -Wall -Werror -Wextra -Wno-unused-variable
LDFLAGS += -shared -lmicrovmi
DEPS = 
OBJ = leechcore_device_microvmi.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

leechcore_device_microvmi: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) -o leechcore_device_microvmi.so $(LDFLAGS)
	rm *.o
	mkdir -p ../files
	mv leechcore_device_microvmi.so ../files/

clean:
	rm -f *.o
