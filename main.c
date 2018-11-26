#include <stdio.h>
#include <stdlib.h>
#include "stm32f4xx_conf.h"
#include <stm32f4_discovery.h>
#include "main.h"
#include "systick.h"
#include "utils.h"
#include "led.h"
#include "ff.h"


#define         FILE_NAME                       "loader.bin"
#define         APP_ADDRESS			0x08004000

typedef void (*pfunc) (void);
static void update_firmware(void);


int main(void)
{
    led_init();
    systick_init();
    update_firmware();
}


static void update_firmware(void)
{
    char buf[256];
    FATFS fatfs;		/* File system object */
    FRESULT rc;			/* Result code */
    FIL fil;			/* File object */
    unsigned bw = 10;
    u32 addr;
    int bytes, i;


    do {
	/* Монтируем. Если нет файла - выходим на загрузку  */
	if ((rc = f_mount(0, &fatfs)) != 0) {
	    break;
	}

	/* Открыть на чтение */
	rc = f_open(&fil, FILE_NAME, FA_READ);
	if (rc != 0) {
	    break;
	}

	/* стираем секторы 2...4 */
	FLASH_Unlock();
	delay_ms(50);

	if (FLASH_COMPLETE == FLASH_EraseSector(FLASH_Sector_1, VoltageRange_3)) {
	    led_toggle(LED3);
	}
        
        
	if (FLASH_COMPLETE == FLASH_EraseSector(FLASH_Sector_2, VoltageRange_3)) {
	    led_toggle(LED3);
	}

	if (FLASH_COMPLETE == FLASH_EraseSector(FLASH_Sector_3, VoltageRange_3)) {
	    led_toggle(LED3);
	}

	if (FLASH_COMPLETE == FLASH_EraseSector(FLASH_Sector_4, VoltageRange_3)) {
	    led_toggle(LED3);
	}


	/* Читаем файл и записываем в секторы flash */
	bytes = 0;
	addr = APP_ADDRESS;
	do {
	    bw = 0;
	    rc = f_read(&fil, buf, sizeof(buf), &bw);
	    if (rc == 0 && bw > 0) {
		bytes += bw;
		for (i = 0; i < bw; i++) {
		    FLASH_ProgramByte(addr, buf[i]);
		    addr++;
		}
		led_toggle(LED4);
		delay_ms(50);
	    }
	} while (bw);

//      rc = f_close(&fil);

	/* Стираем сам файл */
	rc = f_unlink(FILE_NAME);

	FLASH_Lock();
	delay_ms(250);

    } while (0);

    /* Disable all interrupts */
    RCC->CIR = 0x00000000;

    SCB->VTOR = APP_ADDRESS;
    __set_MSP(*(uint32_t *) APP_ADDRESS);
    __DMB();

    (*((pfunc *) (APP_ADDRESS + 4))) ();
}
