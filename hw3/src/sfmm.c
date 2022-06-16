/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "debug.h"
#include "sfmm.h"
#include "helper.h"

// Returns a pointer to allocated memory for the requested size. If the size is invalid, or there is not enough memory to satisfy the request, return NULL;
void *sf_malloc(sf_size_t size) {
    if(size <= 0) return NULL;

    // Calculating block size for request.
    uint32_t blk_size = get_req_blk_size(size);

    // If first call to sf_malloc, then perform heap setup.
    if(sf_mem_start() == sf_mem_end()) {
        if(init_heap() == -1) {return NULL;}
    }

    // Return valid pointer if a quick list block can satisfy request.
    void* quick_list_ptr = search_quicklists(blk_size, size);
    if(quick_list_ptr) {return quick_list_ptr;}

    // Return valid pointer if a free list block can satisfy request.
    void* free_list_ptr = search_freelists(blk_size, size);
    if(free_list_ptr) {return free_list_ptr;}

    // Not enough memory to satisfy request.
    return NULL;
}

// Frees allocated memory for the given block. If the pointer is invalid, the program is aborted.
void sf_free(void *pp) {
    // Check if pointer and block are valid for freeing.
    if(validate_block(pp) == -1) {abort();}

    // Check if the block will be put in quick lists or free lists.
    void* blk_start = pp - 16;
    struct sf_block* blk = (sf_block*) blk_start;

    // Put in quick list.
    if(get_quick_list_idx(get_blk_size(blk)) != -1) {
        // Set quick list bit to 1. (Leave alloc bit as 1 and prev_alloc bit as it was.)
        clear_payload_size(blk);
        add_info_bits(blk, 1);

        // Add block to quick lists.
        add_quick_list_blk(blk, get_blk_size(blk));
    }
    // Put in free list.
    else {
        // Set alloc bit to 0. Leave quick list bit as 0 and prev_alloc bit as it was.)
        clear_payload_size(blk);
        blk->header = (((blk->header) ^ MAGIC) & 0xFFFFFFFFFFFFFFF3) ^ MAGIC;

        // Set footer to match header.
        void* next_blk_start = ((void*) blk) + get_blk_size(blk);
        struct sf_block* next_blk = (sf_block*) next_blk_start;
        next_blk->prev_footer = blk->header;

        // Set next block's prev_alloc bit to 0.
        next_blk->header = (((next_blk->header) ^ MAGIC) & 0xFFFFFFFFFFFFFFF5) ^ MAGIC;

        // If next block has a footer, then set it to match changes.
        if(get_info_bits(next_blk) < 4) {
            void* next_next_blk_start = (((void*) next_blk) + get_blk_size(next_blk));
            struct sf_block* next_next_blk = (sf_block*) next_next_blk_start;
            next_next_blk->prev_footer = next_blk->header;
        }

        // Add block to free lists.
        add_free_list_blk(blk, get_blk_size(blk));

        // Coalesce with adjacent blocks if applicable.
        struct sf_block* merge_prev = coalesce_prev_blk(blk);
        if(merge_prev) {
            coalesce_next_blk(merge_prev);
        }
        else {
            coalesce_next_blk(blk);
        }
    }
}

void *sf_realloc(void *pp, sf_size_t rsize) {
    // Free block is request size is 0.
    if(rsize == 0) {
        sf_free(pp);
        return NULL;
    }

    // Check if pointer and block are valid for reallocation.
    if(validate_block(pp) == -1) {abort();}

    // Check if the request size is larger or smaller than the original size.
    void* blk_start = pp - 16;
    struct sf_block* blk = (sf_block*) blk_start;
    uint64_t old_blk_size = get_blk_size(blk);
    uint64_t old_payload_size = get_payload_size(blk);
    uint64_t new_blk_size = get_req_blk_size(rsize);


    if(old_blk_size == new_blk_size) {
        // Request results in equal block size.

        // Payload size same as before, do nothing.
        if(old_payload_size == rsize) {return pp;}

        // Update header to reflect size change. Since size change is small, padding can be used to satisfy request.
        clear_blk_sizes(blk);
        add_blk_sizes(blk, old_blk_size, rsize);
        return pp;
    }
    else if(old_blk_size < new_blk_size) {
        // Request is greater.

        // Copy old payload to new block.
        void* new_blk_payload = sf_malloc(rsize);
        if(!new_blk_payload) {return NULL;}
        memcpy(new_blk_payload, pp, old_payload_size);

        // Free old block.
        sf_free(pp);

        // Return new block's payload address.
        return new_blk_payload;
    }
    else {
        // Request is smaller.

        // Split block if possible.
        split_alloc_block(blk, new_blk_size, rsize);

        return pp;
    }

    // Out of bounds.
    return NULL;
}

double sf_internal_fragmentation() {
    double payload = 0.0;
    double blk_size = 0.0;
    if(sf_mem_start() == sf_mem_end()) {return 0.0;}

    struct sf_block* curr_blk = (sf_block*) sf_mem_start();
    while(1) {
        uint64_t curr_blk_size = get_blk_size(curr_blk);
        uint64_t curr_payload_size = get_payload_size(curr_blk);
        int alloc = (get_info_bits(curr_blk) < 4) ? 0 : 1;
        if(curr_blk_size == 0 && curr_payload_size == 0 && alloc == 1) {
            break;
        }
        else if(curr_payload_size != 0 && alloc == 1) {
            payload = payload + curr_payload_size;
            blk_size = blk_size + curr_blk_size;
        }

        void* next_blk_start = ((void*) curr_blk) + get_blk_size(curr_blk);
        curr_blk = (sf_block*) next_blk_start;
    }

    if(blk_size == 0.0) {return 0.0;}

    return payload/blk_size;
}

double sf_peak_utilization() {
    double agg_payload = 0.0;
    double current_heap_size = sf_mem_end() - sf_mem_start();
    if(current_heap_size == 0) {return 0.0;}

    struct sf_block* curr_blk = (sf_block*) sf_mem_start();
    while(1) {
        uint64_t curr_blk_size = get_blk_size(curr_blk);
        uint64_t curr_payload_size = get_payload_size(curr_blk);
        int alloc = (get_info_bits(curr_blk) < 4) ? 0 : 1;
        if(curr_blk_size == 0 && curr_payload_size == 0 && alloc == 1) {
            break;
        }
        else if(alloc == 1) {
            agg_payload = agg_payload + curr_payload_size;
        }

        void* next_blk_start = ((void*) curr_blk) + get_blk_size(curr_blk);
        curr_blk = (sf_block*) next_blk_start;
    }

    return agg_payload/current_heap_size;
}
