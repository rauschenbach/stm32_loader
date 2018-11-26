#include "systick.h"


static volatile uint32_t TimingDelay = 0;
static volatile s64 millisex = 0;


/*******************************************************************************
* Function Name  : Инициализация SYSTICK
*******************************************************************************/
void systick_init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    /* Set the Vector Table base address at 0x08000000 */
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x00);

    /* Configure the Priority Group to 2 bits */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    // Setup SysTick Timer for 1 msec interrupts 
    if (SysTick_Config(SystemCoreClock / 1000)) {
	while (1);		// Capture error  
    }

    // Configure the SysTick handler priority 
    NVIC_InitStructure.NVIC_IRQChannel = SysTick_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* Configure the SysTick handler priority */
    NVIC_SetPriority(SysTick_IRQn, 0x0);
}

/*******************************************************************************
* Function Name  : Delay
* Description    : Ввод времени задержки.
*******************************************************************************/
void Delay(uint32_t nCount)
{
    TimingDelay = nCount;

    SysTick->VAL = (uint32_t) 0x0;	//???

    while (TimingDelay != 0) {
	asm("NOP");
    }
}

/*******************************************************************************
* Function Name  : SysTick_Handler
* Description    : This function handles SysTick Handler.
*******************************************************************************/
void TimingDelayDec(void)
{
    millisex ++; /* увеличиваем миллисекунды */

    /* Decrement the TimingDelay variable */
    if (TimingDelay != 0x00) {
	TimingDelay--;
    }
}

/* Установить таймаут в мс */
void set_timeout(int ms)
{
    TimingDelay = ms;
//    SysTick->VAL = (uint32_t) 0x0;	//???
}

/* Проверить, прошел таймаут или не */
bool is_timeout(void)
{
    volatile bool res = false;

    if (TimingDelay == 0)
	res = true;
    return res;	
}

void clr_timeout(void)
{
    TimingDelay = 0;
}

s64 get_msex(void)
{
  return millisex;
}

