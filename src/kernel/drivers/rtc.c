#include "drivers/rtc.h"
#include "io.h"
#include <stdbool.h>
#include <string.h>

#define CMOS_ADDR_PORT 0x70
#define CMOS_DATA_PORT 0x71

/* CMOS RTC Registers */
#define RTC_REG_SECONDS  0x00
#define RTC_REG_MINUTES  0x02
#define RTC_REG_HOURS    0x04
#define RTC_REG_DAY      0x07
#define RTC_REG_MONTH    0x08
#define RTC_REG_YEAR     0x09
#define RTC_REG_CENTURY  0x32 /* Standard ACPI/BIOS century register */
#define RTC_REG_STATUS_A 0x0A
#define RTC_REG_STATUS_B 0x0B

static uint64_t s_boot_epoch_sec = 0;

static inline uint8_t cmos_read(uint8_t reg) {
    /* Preserve NMI mask bit (bit 7) */
    uint8_t prev = inb(CMOS_ADDR_PORT);
    outb(CMOS_ADDR_PORT, (prev & 0x80) | (reg & 0x7F));
    return inb(CMOS_DATA_PORT);
}

static inline bool rtc_is_updating(void) {
    return (cmos_read(RTC_REG_STATUS_A) & 0x80) != 0;
}

static inline uint8_t bcd_to_bin(uint8_t val) {
    return (uint8_t)((val & 0x0F) + ((val >> 4) * 10));
}

static void rtc_read_raw(rtc_time_t *t) {
    /* Bounded wait (~100,000 iterations) so emulated hardware never hangs */
    for (uint32_t timeout = 0; timeout < 100000; timeout++) {
        if (!rtc_is_updating()) {
            break;
        }
        __asm__ volatile("pause");
    }

    t->second = cmos_read(RTC_REG_SECONDS);
    t->minute = cmos_read(RTC_REG_MINUTES);
    t->hour   = cmos_read(RTC_REG_HOURS);
    t->day    = cmos_read(RTC_REG_DAY);
    t->month  = cmos_read(RTC_REG_MONTH);
    t->year   = cmos_read(RTC_REG_YEAR);
}

void rtc_read_time(rtc_time_t *out) {
    if (!out) return;

    rtc_time_t t1, t2;
    int retries = 10;

    do {
        rtc_read_raw(&t1);
        rtc_read_raw(&t2);
    } while (memcmp(&t1, &t2, sizeof(rtc_time_t)) != 0 && --retries > 0);
    uint8_t reg_b = cmos_read(RTC_REG_STATUS_B);
    uint8_t century = cmos_read(RTC_REG_CENTURY);

    /* Convert BCD to binary if bit 2 of Status Register B is clear */
    if (!(reg_b & 0x04)) {
        t1.second = bcd_to_bin(t1.second);
        t1.minute = bcd_to_bin(t1.minute);
        t1.hour   = bcd_to_bin(t1.hour & 0x7F) | (t1.hour & 0x80);
        t1.day    = bcd_to_bin(t1.day);
        t1.month  = bcd_to_bin(t1.month);
        t1.year   = bcd_to_bin((uint8_t)t1.year);
        if (century != 0) {
            century = bcd_to_bin(century);
        }
    }

    /* Convert 12-hour clock to 24-hour clock if bit 1 of Status Register B is clear */
    if (!(reg_b & 0x02) && (t1.hour & 0x80)) {
        t1.hour = (uint8_t)(((t1.hour & 0x7F) + 12) % 24);
    }

    /* Reconstruct full 4-digit year */
    if (century >= 19 && century <= 21) {
        t1.year += (uint16_t)century * 100;
    } else {
        t1.year += 2000;
    }

    *out = t1;
}

static bool is_leap_year(uint16_t year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint64_t rtc_get_unix_timestamp(void) {
    rtc_time_t t;
    rtc_read_time(&t);

    static const uint16_t days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };

    uint64_t days = 0;

    for (uint16_t y = 1970; y < t.year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    if (t.month >= 1 && t.month <= 12) {
        days += days_before_month[t.month - 1];
        if (t.month > 2 && is_leap_year(t.year)) {
            days += 1;
        }
    }

    if (t.day >= 1) {
        days += (uint64_t)(t.day - 1);
    }

    return (days * 86400ULL)
         + ((uint64_t)t.hour * 3600ULL)
         + ((uint64_t)t.minute * 60ULL)
         + (uint64_t)t.second;
}

void rtc_init(void) {
    s_boot_epoch_sec = rtc_get_unix_timestamp();
}

uint64_t rtc_get_boot_epoch(void) {
    return s_boot_epoch_sec;
}