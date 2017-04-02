#ifndef MALLOC_H
#define MALLOC_H
#include <sys/types.h>

int posix_memalign(void** ptr, size_t alignment, size_t size);
void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);
void print_mem_structs();

#endif
