#ifndef _SYSTICK_H
#define _SYSTICK_H

#include "main.h"
#include "globdefs.h"

void systick_init(void);
void set_timeout(int);
bool is_timeout(void);
void clr_timeout(void);

void TimingDelayDec(void);
void Delay(__IO uint32_t nCount);
s64 get_msex(void);


#define delay_ms(x)	Delay(x)
#define delay_sec(x)	Delay(x * 1000L)

#endif /* _STM32_BOARD_H */
