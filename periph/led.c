#include <stm32f4_discovery.h>
#include "led.h"



/* Инициализация LED  */
void led_init(void)
{
    STM_EVAL_LEDInit(LED3);
    STM_EVAL_LEDInit(LED4);
    STM_EVAL_LEDInit(LED5);
    STM_EVAL_LEDInit(LED6);
}

void led_on(int led)
{
    STM_EVAL_LEDOn(led);
}


void led_off(int led)
{
   STM_EVAL_LEDOff(led);
}

void led_toggle(int led)
{
    STM_EVAL_LEDToggle(led);
}
