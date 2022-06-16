#include <stdio.h>
#include <errno.h>
#include "sfmm.h"
#include "helper.h"

// sf_mem_grow wrapper with error handling.
void* safe_sf_mem_grow() {
    void* new_page = sf_mem_grow();
    if(!new_page) {
        sf_errno = ENOMEM;
        return NULL;
    }
    return new_page;
}

// Initialize the heap.
int init_heap() {
    void* new_page = safe_sf_mem_grow();
    if(!new_page) {return -1;}

    // Create prologue: size 32, only alloc bit set.
    struct sf_block* prologue_blk = (sf_block*) sf_mem_start();
    prologue_blk->header = (32 | 4) ^ MAGIC;

    // Create epilogue: size 0, only alloc bit set.
    struct sf_block* epilogue_blk = (sf_block*) (sf_mem_end() - 16);
    epilogue_blk->header = 4 ^ MAGIC;

    // Create quick lists and free lists.
    init_quick_lists();
    init_free_lists();

    // Store the remaining memory (976 bytes) into a block.
    struct sf_block* rem_blk = (sf_block*) (sf_mem_start() + 32);
    clear_blk_sizes(rem_blk);
    clear_info_bits(rem_blk);
    add_blk_sizes(rem_blk, (uint64_t) 976, 0);
    add_info_bits(rem_blk, 2);
    epilogue_blk->prev_footer = rem_blk->header;

    // Place into free list.
    add_free_list_blk(rem_blk, 976);

    return 0;
}

// Extends the heap by 1024 bytes.
int add_mem_page() {
    void* new_page = safe_sf_mem_grow();
    if(!new_page) {return -1;}

    // Build new block on top of old epilogue area.
    struct sf_block* new_mem = (sf_block*) (new_page - 16);
    clear_blk_sizes(new_mem);
    new_mem->header = ((new_mem->header ^ MAGIC) & 2) ^ MAGIC;
    add_blk_sizes(new_mem, (uint64_t) 1024, 0);

    // Create new epilogue.
    struct sf_block* epilogue_blk = (sf_block*) (sf_mem_end() - 16);
    clear_blk_sizes(epilogue_blk);
    clear_info_bits(epilogue_blk);
    add_info_bits(epilogue_blk, 4);
    epilogue_blk->prev_footer = new_mem->header;

    // Put new block into free lists.
    add_free_list_blk(new_mem, 1024);

    // Perform coalescing.
    coalesce_prev_blk(new_mem);

    return 0;
}

// Given the size of the requested memory, return the size of the entire block.
uint32_t get_req_blk_size(sf_size_t size) {
    uint32_t blk_size = size + 8;
    while(blk_size % 16 != 0) {blk_size++;}
    if(blk_size < 32) {blk_size = blk_size + 16;}
    return blk_size;
}

// Returns the info bits of the previous block.
uint64_t get_prev_blk_size(sf_block* blk) {
    return (blk->prev_footer ^ MAGIC) & 0x00000000FFFFFFF0;
}

// Returns the size of a valid block.
uint64_t get_blk_size(sf_block* blk) {
    return (blk->header ^ MAGIC) & 0x00000000FFFFFFF0;
}

// Returns the payload size of a valid block.
uint64_t get_payload_size(sf_block* blk) {
    return ((blk->header ^ MAGIC) & 0xFFFFFFFF00000000) >> 32;
}

// Sets size bits of header field of a valid block.
void add_blk_sizes(sf_block* blk, uint64_t blk_size, uint64_t payload_size) {
    uint64_t sizes = (((uint64_t)(payload_size) << 32) | blk_size);
    blk->header = ((blk->header ^ MAGIC) | sizes) ^ MAGIC;
}

// Clears size bits of header field of a valid block.
void clear_blk_sizes(sf_block* blk) {
    blk->header = ((blk->header ^ MAGIC) & 0x000000000000000F) ^ MAGIC;
}

// Clears payload bits only of header field of a valid block.
void clear_payload_size(sf_block* blk) {
    blk->header = ((blk->header ^ MAGIC) & 0x00000000FFFFFFFF) ^ MAGIC;
}

// Returns the info bits of the previous block.
uint64_t get_prev_info_bits(sf_block* blk) {
    return (blk->prev_footer ^ MAGIC) & 0x000000000000000F;
}

// Returns the info bits of a valid block.
uint64_t get_info_bits(sf_block* blk) {
    return (blk->header ^ MAGIC) & 0x000000000000000F;
}

// Sets info bits of header field of a valid block (3 lsb's).
void add_info_bits(sf_block* blk, int info) {
    blk->header = ((blk->header ^ MAGIC) | info) ^ MAGIC;
}

// Clears info bits of header field of a valid block (3 lsb's).
void clear_info_bits(sf_block* blk) {
    blk->header = ((blk->header ^ MAGIC) & 0xFFFFFFFFFFFFFFF0) ^ MAGIC;
}

// Given the size of a block, return the index of the quicklist it would be in.
int get_quick_list_idx(uint32_t size) {
    if(size < 32) {return -1;}
    else if(size == 32) {return 0;}
    else if(size == 48) {return 1;}
    else if(size == 64) {return 2;}
    else if(size == 80) {return 3;}
    else if(size == 96) {return 4;}
    else if(size == 112) {return 5;}
    else if(size == 128) {return 6;}
    else if(size == 144) {return 7;}
    else if(size == 160) {return 8;}
    else if(size == 176) {return 9;}
    else {return -1;}
}

// Initialize quick lists.
void init_quick_lists() {
    for(int i = 0; i < NUM_QUICK_LISTS; i++) {
        sf_quick_lists[i].first = NULL;
        sf_quick_lists[i].length = 0;
    }
}

// Adds a block to a quick list.
void add_quick_list_blk(sf_block* blk, uint32_t size) {
    struct sf_block* head = sf_quick_lists[get_quick_list_idx(size)].first;
    int length = sf_quick_lists[get_quick_list_idx(size)].length;

    // If quick list is at capacity, flush.
    if(length == QUICK_LIST_MAX) {
        flush_quicklist(get_quick_list_idx(size));
        sf_quick_lists[get_quick_list_idx(size)].first = blk;
        sf_quick_lists[get_quick_list_idx(size)].length++;
        return;
    }

    // Adds block at front of the quick list.
    sf_quick_lists[get_quick_list_idx(size)].first = blk;
    blk->body.links.next = head;

    // Increment length.
    sf_quick_lists[get_quick_list_idx(size)].length++;
}

// Removes all items from a quicklist and adds it to free lists.
void flush_quicklist(int index) {
    for(int i = 0; i < QUICK_LIST_MAX; i++) {
        // Set alloc bit to 0, set quick list bit to 0.
        struct sf_block* curr_blk = sf_quick_lists[index].first;
        curr_blk->header = (((curr_blk->header) ^ MAGIC) & 0xFFFFFFFFFFFFFFF2) ^ MAGIC;

        // Add footer.
        void* next_blk_start = ((void*) curr_blk) + get_blk_size(curr_blk);
        struct sf_block* next_blk = (sf_block*) next_blk_start;
        next_blk->prev_footer = curr_blk->header;

        // Set next block's prev_alloc bit to 0.
        next_blk->header = (((next_blk->header) ^ MAGIC) & 0xFFFFFFFFFFFFFFF5) ^ MAGIC;

        // Set next block's footer to match if it is free.
        if(get_info_bits(next_blk) < 4) {
            void* next_next_blk_start = (((void*) next_blk) + get_blk_size(next_blk));
            struct sf_block* next_next_blk = (sf_block*) next_next_blk_start;
            next_next_blk->prev_footer = next_blk->header;
        }

        // Insert into free list.
        sf_quick_lists[index].first = curr_blk->body.links.next;
        add_free_list_blk(curr_blk, get_blk_size(curr_blk));

        // Coalesce with adjacent blocks if applicable.
        struct sf_block* merge_prev = coalesce_prev_blk(curr_blk);
        if(merge_prev) {
            coalesce_next_blk(merge_prev);
        }
        else {
            coalesce_next_blk(curr_blk);
        }

        sf_show_heap();
    }

    // Reset list.
    sf_quick_lists[index].first = NULL;
    sf_quick_lists[index].length = 0;
}

// Given the size of a block, search quick lists for a block of that exact size. If none found, return NULL.
void* search_quicklists(uint32_t blk_size, uint32_t payload_size) {
    int index = get_quick_list_idx(blk_size);
    if(index == -1) {return NULL;}

    struct sf_block* head = sf_quick_lists[index].first;

    // If block that exactly satisfies request is fulfilled, then remove from quick list and return payload address.
    if(head) {
        // Redirect pointers to remove.
        struct sf_block* next_blk = head->body.links.next;
        sf_quick_lists[index].first = next_blk;

        //Adjust header of the removed block: keep 1 in alloc bit, keep prev_alloc bit, and 0 in quick_list bit. Add block size.
        clear_blk_sizes(head);
        add_blk_sizes(head, (uint64_t) blk_size, (uint64_t) payload_size);
        head->header = ((head->header ^ MAGIC) & ~1) ^ MAGIC;

        // Decrement list length.
        sf_quick_lists[index].length--;

        return &(head->body.payload);
    }

    return NULL;
}


// Given the size of a block, return the index of the freelist it would be in.
int get_free_list_idx(uint32_t size) {
    if(size < 32) {return -1;}
    else if(size == 32) {return 0;}
    else if(size <= 64) {return 1;}
    else if(size <= 128) {return 2;}
    else if(size <= 256) {return 3;}
    else if(size <= 512) {return 4;}
    else if(size <= 1024) {return 5;}
    else if(size <= 2048) {return 6;}
    else if(size <= 4096) {return 7;}
    else if(size <= 8192) {return 8;}
    else {return 9;}
}

// Initialize free lists.
void init_free_lists() {
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
        sf_free_list_heads[i].body.links.prev = sf_free_list_heads[i].body.links.next =  &sf_free_list_heads[i];
    }
}

// Adds a block to a free list.
void add_free_list_blk(sf_block* blk, uint32_t size) {
    struct sf_block* sentinel = &sf_free_list_heads[get_free_list_idx(size)];
    struct sf_block* next = sentinel->body.links.next;
    sentinel->body.links.next = next->body.links.prev = blk;
    blk->body.links.prev = sentinel;
    blk->body.links.next = next;
}

// Deletes a block from a free list.
void delete_free_list_blk(sf_block* blk, uint32_t size) {
    struct sf_block* current_blk = &sf_free_list_heads[get_free_list_idx(size)];
    while(current_blk != blk) {
        current_blk= current_blk->body.links.next;
    }
    struct sf_block* prev_blk = current_blk->body.links.prev;
    struct sf_block* next_blk = current_blk->body.links.next;
    prev_blk->body.links.next = next_blk;
    next_blk->body.links.prev = prev_blk;
    current_blk->body.links.prev = current_blk->body.links.next = NULL;
}

// Relocates block after change in size, if necessary.
void relocate_free_list_blk(sf_block* blk, uint32_t old_size, uint32_t new_size) {
    // If block needs to be relocated.
    if(get_free_list_idx(old_size) != get_free_list_idx(new_size)) {
        // Delete from old list.
        delete_free_list_blk(blk, old_size);

        // Add to new list.
        add_free_list_blk(blk, new_size);
    }
}

// Given the size of a block, search free lists for the smallest block that satisfies the request.
// If there is no block large enough to satisfy the request, then extend heap. If heap space is exhausted, return NULL.
void* search_freelists(uint32_t blk_size, uint32_t payload_size) {
    while(1) {
        int start_idx = get_free_list_idx(blk_size);
        for(int i = start_idx; i < NUM_FREE_LISTS; i++) {
            // Iterate over blocks of current head.
            struct sf_block* sentinel = &sf_free_list_heads[i];
            struct sf_block* curr_blk = sentinel->body.links.next;
            while(curr_blk != sentinel) {
                // If satisfactory block is found but it can be split without a splinter.
                if(get_blk_size(curr_blk) >= (blk_size + 32)) {
                    split_free_block(curr_blk, blk_size, payload_size);
                    return &(curr_blk->body.payload);
                }
                // If satisfactory block is found of exact same size, or it cannot be split without a splinter.
                else if(get_blk_size(curr_blk) >= blk_size) {
                    delete_free_list_blk(curr_blk, get_blk_size(curr_blk));

                    // Adjust header of the removed block: set alloc bit to 1, keep prev_alloc bit, and keep 0 in quick_list bit.
                    clear_payload_size(curr_blk);
                    add_blk_sizes(curr_blk, (uint64_t) get_blk_size(curr_blk), (uint64_t) payload_size);
                    add_info_bits(curr_blk, 4);

                    // Adjust header of next block: set prev_alloc bit to 1.
                    struct sf_block* next_blk = (sf_block*) ((void *) curr_blk + get_blk_size(curr_blk));
                    add_info_bits(next_blk, 2);

                    // Set next block's footer to match if it is free.
                    if(get_info_bits(next_blk) < 4) {
                    	void* next_next_blk_start = (((void*) next_blk) + get_blk_size(next_blk));
                    	struct sf_block* next_next_blk = (sf_block*) next_next_blk_start;
                    	next_next_blk->prev_footer = next_blk->header;
                    }

                    return &(curr_blk->body.payload);
                }

                curr_blk = curr_blk->body.links.next;
            }
        }
        if(add_mem_page() == - 1) {break;}
    }
    return NULL;
}

// Given a pointer to the payload of a block, check if the pointer is valid. Then check if the block can be freed.
int validate_block(void* ptr) {
    // If pointer is NULL or not 16 byte aligned, return -1.
    if(!ptr ||((uint64_t) ptr) % 16 != 0) {return -1;}

    // If the header of the block preceeds the header of the first block in the heap, return -1.
    uint64_t blk_start = ((uint64_t) ptr) - 16;
    uint64_t first_blk_start = ((uint64_t) sf_mem_start()) + 8;
    if(blk_start < first_blk_start) {return -1;}

    // If the block size is less than 32 or not a multiple of 16, return -1.
    struct sf_block* blk = (sf_block*) ((void *) blk_start);
    if(get_blk_size(blk) < 32 || (get_blk_size(blk) % 16 != 0)) {return -1;}

    // If the block is free, return -1;
    if(get_info_bits(blk) < 4) {return -1;}

    return 0;
}

// Given a valid free block of memory, split it into two blocks, one of req_size and the other of blk_size - req_size.
void split_free_block(sf_block* blk, uint32_t blk_size, uint32_t payload_size) {
    uint64_t presplit_size = get_blk_size(blk);

    // The lower block will be returned for caller usage.
    clear_blk_sizes(blk);
    add_blk_sizes(blk, (uint64_t) blk_size, (uint64_t) payload_size);
    add_info_bits(blk, 4);

    // Higher block will be resized.
    void* higher_blk_start = (((void *) blk) + blk_size);
    struct sf_block* higher_blk = (sf_block*) higher_blk_start;
    clear_blk_sizes(higher_blk);
    clear_info_bits(higher_blk);
    add_blk_sizes(higher_blk, (uint64_t) (presplit_size - blk_size), 0);
    add_info_bits(higher_blk, 2);

    // Set footer of higher block.
    void* next_blk_start = ((void*) higher_blk + get_blk_size(higher_blk));
    struct sf_block* next_blk = (sf_block*) next_blk_start;
    next_blk->prev_footer = higher_blk->header;

    // Set next block's footer to match if it is free.
    if(get_info_bits(next_blk) < 4) {
        void* next_next_blk_start = ((void*) next_blk) + get_blk_size(next_blk);
        struct sf_block* next_next_blk = (sf_block*) next_next_blk_start;
        next_next_blk->prev_footer = next_blk->header;
    }

    // Delete old free block from lists, add new free block to lists.
    delete_free_list_blk(blk, presplit_size);
    add_free_list_blk(higher_blk, (presplit_size - blk_size));
}

void split_alloc_block(sf_block* blk, uint32_t blk_size, uint32_t payload_size) {
    uint64_t presplit_size = get_blk_size(blk);

    // If splinter, do not create free block.
    if((presplit_size - blk_size) < 32) {
        // Adjust header and return.
        clear_blk_sizes(blk);
        add_blk_sizes(blk, presplit_size, (uint64_t) payload_size);
        return;
    }

    // The lower part of the block will be adjusted and returned.
    clear_blk_sizes(blk);
    add_blk_sizes(blk, (uint64_t) blk_size, (uint64_t) payload_size);

    // The higher part of the block will be freed.
    void* higher_blk_start = (((void *) blk) + blk_size);
    struct sf_block* higher_blk = (sf_block*) higher_blk_start;
    clear_blk_sizes(higher_blk);
    clear_info_bits(higher_blk);
    add_blk_sizes(higher_blk, (uint64_t) (presplit_size - blk_size), 0);
    add_info_bits(higher_blk, 2);

    // Set footer of higher block.
    void* next_blk_start = ((void*) higher_blk + get_blk_size(higher_blk));
    struct sf_block* next_blk = (sf_block*) next_blk_start;
    next_blk->prev_footer = higher_blk->header;

    // Set next block's footer to match if it is free.
    if(get_info_bits(next_blk) < 4) {
        void* next_next_blk_start = ((void*) next_blk) + get_blk_size(next_blk);
        struct sf_block* next_next_blk = (sf_block*) next_next_blk_start;
        next_next_blk->prev_footer = next_blk->header;
    }

    // Add new free block to lists.
    add_free_list_blk(higher_blk, (presplit_size - blk_size));

    // Coalesce with adjacent blocks if applicable.
    coalesce_next_blk(higher_blk);
}

// Given a block, attempt to coalesce with previous block.
sf_block* coalesce_prev_blk(sf_block* blk) {
    // Do not coalesce if previous block is allocated.
    if((get_info_bits(blk) & 2) == 2) {return NULL;}

    // Calculate merged sizes of the blocks.
    uint64_t prev_size = get_prev_blk_size(blk);
    uint64_t current_size = get_blk_size(blk);
    uint64_t merge_size = prev_size + current_size;

    // Create merged block.
    struct sf_block* merged_block = (sf_block*) (((void *) blk) - prev_size);
    clear_blk_sizes(merged_block);
    add_blk_sizes(merged_block, (uint64_t) merge_size, 0);

    // Replace next block's prev_footer field with updated information.
    struct sf_block* next_blk = (sf_block*) (((void *) merged_block) + merge_size);
    next_blk->prev_footer = merged_block->header;

    // Remove old block from free lists.
    delete_free_list_blk(blk, current_size);

    // If the merged block no longer belongs to the same free list due to an increase in size, move to appropriate free list.
    relocate_free_list_blk(merged_block, prev_size, merge_size);

    return merged_block;
}

void coalesce_next_blk(sf_block* blk) {
    void* next_blk_start = ((void*) blk) + get_blk_size(blk);
    struct sf_block* next_blk = (sf_block*) next_blk_start;

    // Do not coalesce if next block is allocated.
    if((get_info_bits(next_blk) & 4) == 4) {return;}

    // Calculate merged sizes of the blocks.
    uint64_t current_size = get_blk_size(blk);
    uint64_t next_size = get_blk_size(next_blk);
    uint64_t merge_size = current_size + next_size;

    // Create merged block.
    clear_blk_sizes(blk);
    add_blk_sizes(blk, (uint64_t) merge_size, 0);

    // Replace block after merged block's prev_footer field with updated information.
    struct sf_block* merged_next_blk = (sf_block*) (((void *) blk) + merge_size);
    merged_next_blk->prev_footer = blk->header;

    // Remove old block from free lists.
    delete_free_list_blk(next_blk, next_size);

    // If the merged block no longer belongs to the same free list due to an increase in size, move to appropriate free list.
    relocate_free_list_blk(blk, current_size, merge_size);
}