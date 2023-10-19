//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _PXA_TIMR_H_
#define _PXA_TIMR_H_

#include "CPU.h"
#include "mem.h"
#include "soc_IC.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PxaTimr;

struct PxaTimr* pxaTimrInit(struct ArmMem* physMem, struct SocIc* ic);
void pxaTimrTick(struct PxaTimr* timr);

#ifdef __cplusplus
}
#endif

#endif
