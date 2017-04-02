#ifndef MEM_ARENA_H
#define MEM_ARENA_H

#include <bsd/sys/cdefs.h>
#include <bsd/sys/queue.h>
#include <stddef.h>
#include <pthread.h>
#include "mem_block.h"

#define __uintptr_t uintptr_t
#define PAGE_SIZE getpagesize()
#define DEFAULT_ARENA_SIZE  PAGE_SIZE*4
#define MB_STRUCT_RSIZE  offsetof(mem_block_t, mb_data)

typedef struct mem_arena
{
    LIST_ENTRY(mem_arena) ma_list;
    LIST_HEAD(, mem_block) ma_freeblks;
    // arena size minus sizeof(mem_arena_t)
    size_t size;
    mem_block_t ma_first;
} mem_arena_t;

LIST_HEAD(, mem_arena) arena_list;

int create_arena(mem_arena_t** arena, size_t arena_size);
void destroy_arena(mem_arena_t* arena);

mem_arena_t* find_arena(uint64_t address);
mem_block_t* find_prev_free_block(mem_block_t* block);

void concat_free_blocks(mem_block_t* left, mem_block_t* right);
mem_block_t* align_free_block(mem_arena_t* arena, mem_block_t* block, size_t alignment);
mem_block_t* split_free_block(mem_block_t* block, size_t size, size_t alignment);
void reduce_block(mem_arena_t* arena, mem_block_t* block, size_t size);

mem_block_t* fill_chunk(mem_block_t* block, size_t size, size_t alignment);
mem_block_t* fill_whole(mem_arena_t* arena, mem_block_t* block, size_t alignment);

void* allocate_block(mem_arena_t* arena, size_t allocation_size, size_t alignment);
void* allocate_big_block(size_t allocation_size, size_t alignment);
#endif

