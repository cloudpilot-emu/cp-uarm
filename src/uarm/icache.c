//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include "icache.h"

#include <stdlib.h>
#include <string.h>

#include "CPU.h"
#include "uarm_endian.h"
#include "util.h"

#define CACHE_LINE_WIDTH_BITS 5
#define CACHE_INDEX_BITS 20

#define calculateIndex(va) ((va >> CACHE_LINE_WIDTH_BITS) & ~(0xffffffff << CACHE_INDEX_BITS))
#define calculateTag(va) (va >> (CACHE_LINE_WIDTH_BITS + CACHE_INDEX_BITS))
#define calculateLineIndex(va) (va & ~(0xffffffff << CACHE_LINE_WIDTH_BITS))

struct icacheline {
    uint8_t data[1 << CACHE_LINE_WIDTH_BITS];

    uint_fast8_t tag;
    uint32_t revision;
};

struct icache {
    struct ArmMem* mem;
    struct ArmMmu* mmu;

    uint32_t revision;
    struct icacheline cache[1 << CACHE_INDEX_BITS];
};

void icacheInval(struct icache* ic) {
    ic->revision++;

    if (ic->revision == 0) {
        for (size_t i = 0; i < (1 << CACHE_INDEX_BITS); i++) ic->cache[i].revision = 0;
    }
}

struct icache* icacheInit(struct ArmMem* mem, struct ArmMmu* mmu) {
    struct icache* ic = (struct icache*)malloc(sizeof(*ic));

    if (!ic) ERR("cannot alloc icache");

    memset(ic, 0, sizeof(*ic));

    ic->mem = mem;
    ic->mmu = mmu;

    icacheInval(ic);

    return ic;
}

void icacheInvalAddr(struct icache* ic, uint32_t va) {
    struct icacheline* line = ic->cache + calculateIndex(va);

    if (line->revision != ic->revision || line->tag != calculateTag(va)) return;

    line->revision = ic->revision - 1;
}

bool icacheFetch(struct icache* ic, uint32_t va, uint_fast8_t sz, uint_fast8_t* fsrP, void* buf) {
    if (va & (sz - 1)) {  // alignment issue

        if (fsrP) *fsrP = 3;
        return false;
    }

    struct icacheline* line = ic->cache + calculateIndex(va);
    const uint_fast8_t tag = calculateTag(va);

    if (line->revision != ic->revision || line->tag != tag) {
        uint32_t pa;
        uint8_t mappingInfo;
        struct ArmMemRegion* region;

        if (!mmuTranslate(ic->mmu, va, true, false, &pa, fsrP, &mappingInfo, &region)) return false;

        if ((mappingInfo & MMU_MAPPING_CACHEABLE) == 0) {
            bool ok = region ? region->aF(region->uD, pa, sz, false, buf)
                             : memAccess(ic->mem, pa, sz, MEM_ACCESS_TYPE_READ, buf);
            if (!ok) {
                if (fsrP) *fsrP = 0x0d;  // perm error
                return false;
            }

            return true;
        }

        if (!memAccess(ic->mem, pa & 0xffffffe0, 32, MEM_ACCESS_TYPE_READ, line->data)) {
            if (fsrP) *fsrP = 0x0d;  // perm error
            return false;
        };

        line->revision = ic->revision;
        line->tag = tag;
    }

    switch (sz) {
        case 4:
            *(uint32_t*)buf = *(uint32_t*)(line->data + calculateLineIndex(va));
            break;

        case 2: {
            *(uint16_t*)buf = *(uint16_t*)(line->data + calculateLineIndex(va));

            break;
        }

        default:
            memcpy(buf, line->data + calculateLineIndex(va), sz);
    }

    return true;
}
