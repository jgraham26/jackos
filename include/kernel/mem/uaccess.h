#pragma once

#include <stdint.h>
#include <stdbool.h>

#define USER_SPACE_TOP 0x00007FFFFFFFFFFFULL

static inline bool validate_user_buffer(const void *buf, size_t count) {
    if (!buf && count > 0) return false;
    uintptr_t start = (uintptr_t)buf;
    uintptr_t end = start + count;

    /* Check for overflow wrap-around and higher-half kernel memory access */
    if (end < start) return false;
    if (end > USER_SPACE_TOP) return false;

    return true;
}