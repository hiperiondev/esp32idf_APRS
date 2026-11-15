#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define MALLOC_CAP_8BIT 4
#define MALLOC_CAP_INTERNAL 0x800
#define MALLOC_CAP_DEFAULT 0x1000
typedef struct { size_t total_free_bytes, total_allocated_bytes, largest_free_block, minimum_free_bytes, allocated_blocks, free_blocks, total_blocks; } multi_heap_info_t;
size_t heap_caps_get_free_size(uint32_t c);
size_t heap_caps_get_minimum_free_size(uint32_t c);
size_t heap_caps_get_largest_free_block(uint32_t c);
size_t heap_caps_get_total_size(uint32_t c);
void heap_caps_get_info(multi_heap_info_t *i, uint32_t c);
bool heap_caps_check_integrity_all(bool p);
void *heap_caps_malloc(size_t s, uint32_t c);
