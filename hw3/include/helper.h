#ifndef HELPER_H
#define HELPER_H

void* safe_sf_mem_grow();
int init_heap();
int add_mem_page();

uint32_t get_req_blk_size(sf_size_t size);
uint64_t get_prev_blk_size(sf_block* blk);
uint64_t get_blk_size(sf_block* blk);
uint64_t get_payload_size(sf_block* blk);
void add_blk_sizes(sf_block* blk, uint64_t blk_size, uint64_t payload_size);
void clear_blk_sizes(sf_block* blk);
void clear_payload_size(sf_block* blk);
uint64_t get_prev_info_bits(sf_block* blk);
uint64_t get_info_bits(sf_block* blk);
void add_info_bits(sf_block* blk, int info);
void clear_info_bits(sf_block* blk);

int get_quick_list_idx(uint32_t size);
void init_quick_lists();
void add_quick_list_blk(sf_block* blk, uint32_t size);
void flush_quicklist(int index);
void* search_quicklists(uint32_t blk_size, uint32_t payload_size);

int get_free_list_idx(uint32_t size);
void init_free_lists();
void add_free_list_blk(sf_block* blk, uint32_t size);
void delete_free_list_blk(sf_block* blk, uint32_t size);
void relocate_free_list_blk(sf_block* blk, uint32_t old_size, uint32_t new_size);
void* search_freelists(uint32_t size, uint32_t payload_size);

int validate_block(void* ptr);
void split_free_block(sf_block* blk, uint32_t blk_size, uint32_t payload_size);
void split_alloc_block(sf_block* blk, uint32_t blk_size, uint32_t payload_size);
sf_block* coalesce_prev_blk(sf_block* blk);
void coalesce_next_blk(sf_block* blk);

#endif