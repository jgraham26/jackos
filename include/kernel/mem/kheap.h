#pragma once

#include <stddef.h>
#include <stdint.h>

void kheap_init(uintptr_t virt_start, size_t initial_bytes, size_t max_bytes);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);
void *kcalloc(size_t num, size_t size);