#ifndef MEM_BLOCK_H
#define MEM_BLOCK_H

#include <bsd/sys/queue.h>
#include <sys/types.h>
#include <stdint.h>

LIST_HEAD(mb_list_head, mem_block);

typedef struct mem_block 
{
    LIST_ENTRY(mem_block) mb_list;
    // mb_size = 16 + space ahead of block
    // mb_size > 0 => block free
    // mb_size < 0 => block allocated
    ssize_t mb_size;
    union {
        // valid if block is free
        LIST_ENTRY(mem_block) mb_free_list;
        // valid if block is allocated
        uint64_t mb_data[0]; //
    };
} mem_block_t;

#endif
