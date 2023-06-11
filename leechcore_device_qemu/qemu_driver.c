#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#define PLUGIN_URL_SCHEME "qemu://"
#define DEFAULT_MAP_SIZE 512*1024*1024 /* 512M FIXME We should get this from init*/ 

typedef struct {
	uint64_t size;
	char *path;
} qemu_ram_t;

uint64_t ram_size = 0;

static bool parse_url_args(const char * url_device, qemu_ram_t *init_params) {
    // this function parses the device URL string and fills the init_params struct in consequence
    // the URL syntax is the following
    //      qemu://path=qemu-ram&size=512

    // check URL scheme
    if (strncmp(url_device, PLUGIN_URL_SCHEME, strlen(PLUGIN_URL_SCHEME))) {
        // no match, quit
        return false;
    }

    // clone URL as strtok will modify it in place
    char *szDevice_start_params =
        strdup(url_device + strlen(PLUGIN_URL_SCHEME));
    // split on '&'
    char *saveptr = NULL;
    char *token = NULL;
    for (token = strtok_r(szDevice_start_params, "&", &saveptr); token != NULL;
         token = strtok_r(NULL, "&", &saveptr)) {
        // token is param1=value1
        // split on '='
        char *saveptr2 = NULL;
        char *param_name = strtok_r(token, "=", &saveptr2);
        if (!param_name)
            continue;
        char *param_value = strtok_r(NULL, "=", &saveptr2);
        if (!param_value)
            continue;
        if (!strncmp(param_name, "size", strlen("size"))) {
            // clear previous value if any
            if (init_params->size) {
                init_params->size = 0;
            }
            init_params->size = atoi(strdup(param_value));
        } else if (!strncmp(param_name, "path", strlen("path"))) {
            // free previous value if any
            if (init_params->path) {
                free(init_params->path);
                init_params->path = NULL;
            }
            init_params->path = strdup(param_value);
        } else {
		printf(url_device, "QEMU: unhandled init parameter: %s\n", param_name);
        }
    }
    free(szDevice_start_params);
    return true;
}




void *qemu_init(const char *url_device){
	int fd = 0;
    	// handle init args
    	qemu_ram_t init_params = {0};
	void * qram = 0;
    	if (!parse_url_args(url_device, &init_params)) {
        	return false;
	}

	ram_size = ( init_params.size != 0 ) ? init_params.size : DEFAULT_MAP_SIZE;
	
	printf("mem_path %s.\n", init_params.path);
	if ((fd = shm_open(init_params.path, O_RDWR | O_SYNC, 0)) < 0) {
    		printf("shm_open(%s) failed", init_params.path);
    		return NULL;
	}
	
	qram = mmap(NULL, ram_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (qram == MAP_FAILED) {
    		printf("Memory map failed");
    		return NULL;
	}
	printf("Memory mapped at address %p.\n", qram);
	return qram;
}

bool qemu_read_physical(void *qram, uint64_t physical_address, uint8_t *buffer, size_t size, uint64_t * bytes_read){

	void *virt_addr = qram + (physical_address & (ram_size -1));	
	memcpy(buffer, (char *) virt_addr, size);
	*bytes_read = size;
	return true;
}


bool qemu_write_physical(void *qram, uint64_t physical_address, uint8_t *buffer, size_t size){

	size_t i = 0;
	void *virt_addr = qram + (physical_address & (ram_size -1));
	memcpy((char *) virt_addr, buffer, size);
	return true;
}


bool qemu_get_max_physical_addr(void *qram, uint64_t *address_ptr){
	*address_ptr = (uint64_t) qram + ram_size - 1;
	return true;
}


void qemu_destroy(void *qram){
	munmap(qram, ram_size);
}
