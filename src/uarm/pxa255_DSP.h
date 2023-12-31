//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _PXA255_DSP_H_
#define _PXA255_DSP_H_

#include "CPU.h"
#include "mem.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Pxa255dsp;

struct Pxa255dsp* pxa255dspInit(struct ArmCpu* cpu);

#ifdef __cplusplus
}
#endif

#endif
