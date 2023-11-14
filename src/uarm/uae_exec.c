#include "uae_exec.h"

#include "uae/UAE.h"
#include "uarm_endian.h"

static struct ArmMem* mem = NULL;
static struct ArmMmu* mmu = NULL;

static uint8_t fsr = 0;
static uint32_t pendingStatus = 0;
static bool priviledged = false;

#ifdef __EMSCRIPTEN__
static cpuop_func* cpufunctbl_base;
#else
static cpuop_func* cpufunctbl[65536];  // (normally in newcpu.c)
#endif

static uint32_t uae_get_le(uint32_t addr, uint8_t size) {
    struct ArmMemRegion* region;
    uint32_t pa;
    if (!mmuTranslate(mmu, addr, priviledged, false, &pa, &fsr, NULL, &region)) return 0;

    uint8_t result;
    bool ok = region ? region->aF(region->uD, pa, size, false, &result)
                     : memAccess(mem, pa, size, MEM_ACCESS_TYPE_READ, &result);

    if (!ok) {
        fsr = 10;  // external abort on non-linefetch
        return 0;
    }

    return result;
}

uint8_t uae_get8(uint32_t addr) {
    fsr = 0;

    return uae_get_le(addr, 1);
}

uint16_t uae_get16(uint32_t addr) {
    fsr = 0;

    if (addr & 0x01) {
        fsr = 1;
        return 0;
    }

    return be16toh(uae_get_le(addr, 2));
}

uint32_t uae_get32(uint32_t addr) {
    fsr = 0;

    if (addr & 0x03) {
        fsr = 1;
        return 0;
    }

    return be32toh(uae_get_le(addr, 4));
}

static void uae_put_le(uint32_t value, uint32_t addr, uint8_t size) {
    struct ArmMemRegion* region;
    uint32_t pa;
    if (!mmuTranslate(mmu, addr, priviledged, false, &pa, &fsr, NULL, &region)) return;

    bool ok = region ? region->aF(region->uD, pa, size, true, &value)
                     : memAccess(mem, pa, size, MEM_ACCESS_TYPE_WRITE, &value);

    if (!ok) {
        fsr = 10;  // external abort on non-linefetch
    }
}

void uae_put8(uint8_t value, uint32_t addr) {
    fsr = 0;

    uae_put_le(value, addr, 1);
};

void uae_put16(uint16_t value, uint32_t addr) {
    fsr = 0;

    if (addr & 0x01) {
        fsr = 1;
        return;
    }

    uae_put_le(htobe16(value), addr, 2);
}

void uae_put32(uint32_t value, uint32_t addr) {
    fsr = 0;

    if (addr & 0x03) {
        fsr = 1;
        return;
    }

    uae_put_le(htobe32(value), addr, 4);
}

void Exception(int exception, uaecptr lastPc) {
    if (exception != uae_status_syscall) regs.pc -= 2;

    pendingStatus = exception;
}

unsigned long op_unimplemented(uint32_t opcode) REGPARAM {
    pendingStatus = uae_status_unimplemented_instr;
    return 0;
}

unsigned long op_illg(uint32_t opcode) REGPARAM {
    pendingStatus = uae_status_illegal_instr;
    return 0;
}

unsigned long op_line1111(uint32_t opcode) REGPARAM {
    pendingStatus = uae_status_line_1111;
    return 0;
}

unsigned long op_line1010(uint32_t opcode) REGPARAM {
    pendingStatus = uae_status_line_1010;
    return 0;
}

void notifiyReturn() { pendingStatus = uae_status_return; }

static void staticInit() {
    static bool initialized = false;
    if (initialized) return;

    int i, j;
    for (i = 0; i < 256; i++) {
        for (j = 0; j < 8; j++) {
            if (i & (1 << j)) {
                break;
            }
        }

        movem_index1[i] = j;
        movem_index2[i] = 7 - j;
        movem_next[i] = i & (~(1 << j));
    }

    read_table68k();
    do_merges();

    // The rest of this code is based on build_cpufunctbl in newcpu.c.

#ifdef __EMSCRIPTEN__
    cpuop_func** cpufunctbl =
        (cpuop_func**)malloc(0x10000 * sizeof(cpuop_func*));  // (normally in newcpu.c)
#endif

    unsigned long opcode;
    struct cputbl* tbl = op_smalltbl_3;

    for (opcode = 0; opcode < 65536; opcode++) {
        if (opcode >> 12 == 0x0f)
            cpufunctbl[opcode] = op_line1111;
        else if (opcode >> 12 == 0x0a)
            cpufunctbl[opcode] = op_line1010;
        else
            cpufunctbl[opcode] = op_illg;
    }

    for (i = 0; tbl[i].handler != NULL; i++) {
        if (!tbl[i].specific) {
            cpufunctbl[tbl[i].opcode] = tbl[i].handler;
        }
    }

    for (opcode = 0; opcode < 65536; opcode++) {
        cpuop_func* f;

        if (table68k[opcode].mnemo == i_ILLG || table68k[opcode].clev > 0) {
            continue;
        }

        if (table68k[opcode].handler != -1) {
            f = cpufunctbl[table68k[opcode].handler];
            if (f == op_illg) {
                abort();
            }

            cpufunctbl[opcode] = f;
        }
    }

    for (i = 0; tbl[i].handler != NULL; i++) {
        if (tbl[i].specific) {
            cpufunctbl[tbl[i].opcode] = tbl[i].handler;
#if HAS_PROFILING
            perftbl[tbl[i].opcode] = tbl[i].perf;
#endif
        }
    }

#ifdef __EMSCRIPTEN__
    cpufunctbl_base = (cpuop_func*)EM_ASM_INT(
        {
            wasmTable.grow(0x10000);

            for (let i = 0; i <= 0xffff; i++)
                wasmTable.set(wasmTable.length - 0xffff - 1 + i,
                              wasmTable.get(HEAPU32[($0 >>> 2) + i]));

            return wasmTable.length - 0xffff - 1;
        },
        cpufunctbl);

    free(cpufunctbl);
#endif

    // (hey readcpu doesn't free this guy!)
    free(table68k);
}

void uaeInit(struct ArmMem* _mem, struct ArmMmu* _mmu) {
    staticInit();

    mem = _mem;
    mmu = _mmu;
}

bool uaeLoad68kState(uint32_t addr) {
    if (addr & 0x03) {
        fsr = 1;
        return false;
    }

    addr += 4;
    fsr = 0;

    for (size_t i = 0; i < 8; i++) {
        regs.regs[i] = uae_get_le(addr, 4);
        addr += 4;

        if (fsr != 0) return false;
    }

    for (size_t i = 0; i < 8; i++) {
        regs.regs[8 + i] = uae_get_le(addr, 4);
        addr += 4;

        if (fsr != 0) return false;
    }

    regs.pc = uae_get_le(addr, 4);
    addr += 4;
    if (fsr != 0) return false;

    regs.sr = uae_get_le(addr, 4);
    MakeFromSR();
    return fsr == 0;
}

bool uaeSave68kState(uint32_t addr) {
    if (addr & 0x03) {
        fsr = 1;
        return false;
    }

    addr += 4;
    fsr = 0;

    for (size_t i = 0; i < 8; i++) {
        uae_put32(regs.regs[i], addr);
        addr += 4;

        if (fsr != 0) return false;
    }

    for (size_t i = 0; i < 8; i++) {
        uae_put32(regs.regs[8 + i], addr);
        addr += 4;

        if (fsr != 0) return false;
    }

    uae_put32(regs.pc, addr);
    addr += 4;
    if (fsr != 0) return false;

    MakeSR();
    uae_put32(regs.sr, addr);
    return fsr == 0;
}

uint8_t uaeGetFsr() { return fsr; }

uint16_t readTrapWord() {
    fsr = 0;
    return uae_get16(regs.pc);
}

void uaeSetPriviledged(bool _priviledged) { priviledged = _priviledged; }

enum uaeStatus uaeExecute() {
    fsr = 0;
    pendingStatus = uae_status_ok;

    uint16_t opcode = uae_get16(regs.pc);
    if (fsr != 0) return uae_status_memory_fault;

#ifdef __EMSCRIPTEN__
    ((cpuop_func*)((long)cpufunctbl_base + opcode))(opcode);
#else
    cpufunctbl[opcode](opcode);
#endif

    return fsr == 0 ? pendingStatus : uae_status_memory_fault;
}