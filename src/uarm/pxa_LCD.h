//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#ifndef _PXA_LCD_H_
#define _PXA_LCD_H_

#include "CPU.h"
#include "clock.h"
#include "mem.h"
#include "soc_IC.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PxaLcd;

struct PxaLcd *pxaLcdInit(struct ArmMem *physMem, struct SocIc *ic, struct Clock *clock,
                          uint16_t width, uint16_t heigh);

uint32_t *pxaLcdGetPendingFrame(struct PxaLcd *lcd);
void pxaLcdResetPendingFrame(struct PxaLcd *lcd);

#ifdef __cplusplus
}
#endif

#endif
