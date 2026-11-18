#pragma once

#include <stdint.h>

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

void rtc_init();
void rtc_read_time(rtc_time_t* out);
uint64_t rtc_get_unix_timestamp();
uint64_t rtc_get_boot_epoch();