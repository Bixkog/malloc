#include <bsd/sys/queue.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include "mem_arena.h"

pthread_mutex_t mem_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
struct mb_list_head* NO_HEAD = NULL;
size_t free_memory = 0;

// add starting point of mmap
int create_arena(mem_arena_t** arena, size_t arena_size)
{
    if(arena_size == 0) arena_size = DEFAULT_ARENA_SIZE;
    *arena = mmap(NULL, 
                arena_size, 
                PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1,
                0);
    if(*arena == MAP_FAILED) return 1;
    // initialize fields
    (*arena)->size = arena_size - sizeof(mem_arena_t); 
    (*arena)->ma_first.mb_size = (*arena)->size + (sizeof(mem_block_t) - MB_STRUCT_RSIZE);
    free_memory += (*arena)->ma_first.mb_size;
    // initialize list
    LIST_INIT(&((*arena)->ma_freeblks));
    LIST_INSERT_HEAD(&((*arena)->ma_freeblks), &((*arena)->ma_first), mb_free_list);
    return 0;
}

void destroy_arena(mem_arena_t* arena)
{
    free_memory -= arena->ma_first.mb_size;
    munmap(arena, arena->size + sizeof(mem_arena_t));
}

// linear to count of arenas
// used in free and realloc
mem_arena_t* find_arena(uint64_t address)
{
    mem_arena_t* current_arena;
    LIST_FOREACH(current_arena, &arena_list, ma_list)
    {
        if((uint64_t) current_arena < address && 
           (uint64_t) current_arena + current_arena->size + sizeof(mem_arena_t) > address)
            return current_arena;
    }
    return NULL;
}

mem_block_t* find_prev_free_block(mem_block_t* block)
{
    mem_block_t* prev_block = NULL;
    block = LIST_PREV(block, NO_HEAD, mem_block, mb_list);
    while(block && block != prev_block && block->mb_size < 0)
    {
        prev_block = block;
        block = LIST_PREV(block, NO_HEAD, mem_block, mb_list);
    }
    return block;
}

void concat_free_blocks(mem_block_t* left, mem_block_t* right)
{
    LIST_REMOVE(right, mb_list);
    LIST_REMOVE(right, mb_free_list);
    left->mb_size += right->mb_size + MB_STRUCT_RSIZE;
    free_memory += MB_STRUCT_RSIZE;
}

// aligns block, reducing its size at most by alignment bytes. Adds them to previous block
// it cant be first block
mem_block_t* align_free_block(mem_arena_t* arena, mem_block_t* block, size_t alignment)
{
    size_t offset = alignment - ((size_t)(block->mb_data) % alignment);
    if(offset == alignment)
        return block;

    block->mb_size -= offset;
    mem_block_t* prev_block = LIST_PREV(block, NO_HEAD, mem_block,  mb_list);
    prev_block->mb_size -= offset; // assuming it can't be free block
    free_memory -= offset;
    
    mem_block_t* new_block_address = (mem_block_t*)((size_t)block + offset);
    mem_block_t* prev_free_block = 
        LIST_PREV(block, &(arena->ma_freeblks), mem_block,  mb_free_list);
    // remove from lists
    LIST_REMOVE(block, mb_list);
    LIST_REMOVE(block, mb_free_list);
    // move block
    memmove(new_block_address, block, sizeof(mem_block_t));
    block = new_block_address;
    // add to lists
    LIST_INSERT_AFTER(prev_block, block, mb_list);
    if(prev_free_block && prev_free_block->mb_size > 0)
        LIST_INSERT_AFTER(prev_free_block, block, mb_free_list);
    else
        LIST_INSERT_HEAD(&(arena->ma_freeblks), block, mb_free_list);
    return block;
}

// split into 2 where second is aligned and has at least needed size
mem_block_t* split_free_block(mem_block_t* block, size_t size, size_t alignment)
{
    // calculate place
    mem_block_t* new_block = (mem_block_t*)((size_t)block->mb_data + block->mb_size 
                                            - size - MB_STRUCT_RSIZE);
    
    // calculate offset
    size_t offset = (size_t)(new_block->mb_data) % alignment;
    // move by offset
    new_block = (mem_block_t*)((size_t)new_block - offset);
    // set sizes
    block->mb_size = (size_t) new_block - (size_t)(block->mb_data);
    new_block->mb_size = (size + offset);
    free_memory -= MB_STRUCT_RSIZE;
    // add to lists
    LIST_INSERT_AFTER(block, new_block, mb_list);
    LIST_INSERT_AFTER(block, new_block, mb_free_list);
    return new_block;
}

// size < abs(block->mb_size)
// used in realloc
void reduce_block(mem_arena_t* arena, mem_block_t* block, size_t size)
{
    size_t old_size = -(block->mb_size);
    // if no space to create new block dont resize. 
    if(old_size - size < sizeof(mem_block_t))
        return;
    mem_block_t* new_block = (mem_block_t*)((size_t)(block->mb_data) + size);
    free_memory -= MB_STRUCT_RSIZE;
    // set size
    new_block->mb_size = old_size - size - MB_STRUCT_RSIZE;
    block->mb_size = -size; // still allocated
    // insert to lists
    LIST_INSERT_AFTER(block, new_block, mb_list);
    mem_block_t* prev_free_block = find_prev_free_block(new_block);
    if(prev_free_block)
        LIST_INSERT_AFTER(prev_free_block, new_block, mb_free_list);    
    else
        LIST_INSERT_HEAD(&(arena->ma_freeblks), new_block, mb_free_list);
}

mem_block_t* fill_chunk(mem_block_t* block, size_t size, size_t alignment)
{
    // allocate block
    mem_block_t* to_allocate = split_free_block(block, size, alignment);
    free_memory -= to_allocate->mb_size;
    to_allocate->mb_size = -(to_allocate->mb_size);
    LIST_REMOVE(to_allocate, mb_free_list);
    return to_allocate;
}

mem_block_t* fill_whole(mem_arena_t* arena, mem_block_t* block, size_t alignment)
{
    block = align_free_block(arena, block, alignment);
    LIST_REMOVE(block, mb_free_list);
    free_memory -= block->mb_size;
    block->mb_size = -(block->mb_size);
    return block;
}

void* allocate_block(mem_arena_t* arena, size_t allocation_size, size_t alignment)
{
    mem_block_t* current_block;
    LIST_FOREACH(current_block, &(arena->ma_freeblks), mb_free_list)
    {
        if(current_block->mb_size > 0 &&   
            (size_t) current_block->mb_size >= allocation_size + alignment)
        {   
            if((size_t) current_block->mb_size >= 
                    allocation_size + 16 + sizeof(mem_block_t) + alignment)
            {
                mem_block_t* to_allocate = fill_chunk(current_block, allocation_size, alignment);
                return (void*) (to_allocate->mb_data);
            }
            else if(current_block != LIST_FIRST(&arena->ma_freeblks)) // corner case
            {
                current_block = fill_whole(arena, current_block, alignment);
                return (void*) (current_block->mb_data);
            }
        }
    }
    return NULL;
}

void* allocate_big_block(size_t allocation_size, size_t alignment)
{

    size_t arena_size;
    if(allocation_size + alignment + sizeof(mem_arena_t) < (size_t) DEFAULT_ARENA_SIZE) 
        arena_size = DEFAULT_ARENA_SIZE;
    else    
        arena_size = (((allocation_size + alignment + sizeof(mem_arena_t)) / 
                        PAGE_SIZE) + 1) * PAGE_SIZE; 
    mem_arena_t* new_arena;
    if(create_arena(&new_arena, arena_size) > 0)
        return NULL;
    // allocate at free arena
    void* ptr = allocate_block(new_arena, allocation_size, alignment);

    // connect arena after alloc
    LIST_INSERT_HEAD(&arena_list, new_arena, ma_list);
    return ptr;
}


