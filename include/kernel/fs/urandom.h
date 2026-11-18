#pragma once

#include <stddef.h>
#include <stdint.h>

void urandom_init();
void urandom_get_bytes(void* buf, size_t len);