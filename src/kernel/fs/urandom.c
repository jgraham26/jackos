#include "fs/urandom.h"
#include "fs/vfs.h"
#include "sched/spinlock.h"
#include "fs/devfs.h"
#include <string.h>
#include <errno.h>
#include "mem/kheap.h"


extern volatile uint64_t g_ticks;

static uint64_t s_rng_state[4];
static spinlock_t s_rng_lock;

static inline uint64_t rdtsc_now() {
    uint32_t hi, lo;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static bool cpu_has_rdrand(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    return (ecx & (1U << 30)) != 0;
}

static bool rdrand64(uint64_t *out) {
    for (int i = 0; i < 10; i++) {
        uint8_t ok = 0;
        uint64_t val = 0;
        __asm__ volatile("rdrand %0; setc %1" : "=r"(val), "=qm"(ok));
        if (ok) {
            *out = val;
            return true;
        }
    }
    return false;
}



static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static inline uint64_t rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro256ss_next(void) {
    uint64_t result = rotl64(s_rng_state[1] * 5, 7) * 9;
    uint64_t t = s_rng_state[1] << 17;

    s_rng_state[2] ^= s_rng_state[0];
    s_rng_state[3] ^= s_rng_state[1];
    s_rng_state[1] ^= s_rng_state[2];
    s_rng_state[0] ^= s_rng_state[3];

    s_rng_state[2] ^= t;
    s_rng_state[3] = rotl64(s_rng_state[3], 45);
    return result;
}

void urandom_get_bytes(void *buf, size_t len) {
    if (!buf || len == 0) return;

    uint64_t flags = spinlock_irqsave(&s_rng_lock);
    /* Mix in current TSC on every call for extra jitter */
    s_rng_state[0] ^= rdtsc_now();

    uint8_t *dst = (uint8_t *)buf;
    size_t i = 0;
    while (i + 8 <= len) {
        uint64_t val = xoshiro256ss_next();
        memcpy(dst + i, &val, 8);
        i += 8;
    }
    if (i < len) {
        uint64_t val = xoshiro256ss_next();
        memcpy(dst + i, &val, len - i);
    }
    spinlock_irqrestore(&s_rng_lock, flags);
}

static int64_t urandom_read(vfs_file_t *file, void *buf, size_t count) {
    (void)file;
    if (!buf) return -EFAULT;
    urandom_get_bytes(buf, count);
    return (int64_t)count;
}

static int64_t urandom_write(vfs_file_t *file, const void *buf, size_t count) {
    (void)file;
    if (!buf) return -EFAULT;

    /* Mix user-written entropy into the PRNG state */
    const uint8_t *src = (const uint8_t *)buf;
    uint64_t flags = spinlock_irqsave(&s_rng_lock);
    for (size_t i = 0; i < count; i++) {
        s_rng_state[i & 3] ^= ((uint64_t)src[i] << ((i & 7) * 8));
    }
    spinlock_irqrestore(&s_rng_lock, flags);
    return (int64_t)count;
}

static const vfs_ops_t s_urandom_ops = {
    .read  = urandom_read,
    .write = urandom_write,
};

static vfs_node_t *create_rng_node(const char *name, uint64_t inode) {
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(vfs_node_t));

    strcpy(node->name, name);
    node->flags = VFS_CHARDEVICE;
    node->inode = inode; /* Major 1, minor 8 (random) / 9 (urandom) */
    node->ops   = &s_urandom_ops;
    return node;
}

void urandom_init(void) {
    uint64_t seed = rdtsc_now() ^ (g_ticks * 0x9e3779b97f4a7c15ULL);
    if (cpu_has_rdrand()) {
        uint64_t hw = 0;
        if (rdrand64(&hw)) seed ^= hw;
    }

    for (int i = 0; i < 4; i++) {
        s_rng_state[i] = splitmix64(&seed);
    }

    /* Register both /dev/urandom and /dev/random in devfs */
    devfs_register_device("urandom", &s_urandom_ops, NULL);
    devfs_register_device("random",  &s_urandom_ops, NULL);
}