#pragma once

#include <stdint.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPAN_DISPLAY     0x4606

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR   2

struct fb_bitfield {
    uint32_t offset;    /* beginning of bitfield (in bits) */
    uint32_t length;    /* length of bitfield (in bits) */
    uint32_t msb_right; /* != 0 : Most significant bit is right */
};

struct fb_fix_screeninfo {
    char     id[16];          /* identification string e.g. "JackOS FB" */
    uint64_t smem_start;      /* Start of frame buffer mem (physical address) */
    uint32_t smem_len;        /* Length of frame buffer mem in bytes */
    uint32_t type;            /* see FB_TYPE_* */
    uint32_t type_aux;        /* Interleave for interleaved Planes */
    uint32_t visual;          /* see FB_VISUAL_* */
    uint16_t xpanstep;        /* zero if no hardware panning */
    uint16_t ypanstep;        /* zero if no hardware panning */
    uint16_t ywrapstep;       /* zero if no hardware ywrap */
    uint16_t _pad0;           /* 4-byte alignment padding before line_length */
    uint32_t line_length;     /* length of a line in bytes (pitch) */
    uint32_t _pad1;           /* 8-byte alignment padding before mmio_start */
    uint64_t mmio_start;      /* Start of Memory Mapped I/O (physical) */
    uint32_t mmio_len;        /* Length of Memory Mapped I/O */
    uint32_t accel;           /* Indicate to driver which specific chip/card */
    uint16_t capabilities;    /* see FB_CAP_* */
    uint16_t reserved[2];     /* Reserved for future compatibility */
    uint16_t _pad2;           /* Pad total struct to 80 (0x50) bytes */
};

struct fb_var_screeninfo {
    uint32_t xres;            /* visible resolution */
    uint32_t yres;
    uint32_t xres_virtual;    /* virtual resolution */
    uint32_t yres_virtual;
    uint32_t xoffset;         /* offset from virtual to visible */
    uint32_t yoffset;

    uint32_t bits_per_pixel;  /* guess what */
    uint32_t grayscale;       /* 0 = color, 1 = grayscale, >1 = FOURCC */

    struct fb_bitfield red;   /* bitfield in fb mem if true color */
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;/* transparency */

    uint32_t nonstd;          /* != 0 Non standard pixel format */
    uint32_t activate;        /* see FB_ACTIVATE_* */
    uint32_t height;          /* height of picture in mm */
    uint32_t width;           /* width of picture in mm */
    uint32_t accel_flags;     /* (OBSOLETE) see fb_info.flags */

    /* Timing: All values in pixclocks, except pixclock (of course) */
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

void fbdev_init();