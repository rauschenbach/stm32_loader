/**
  ******************************************************************************
  * @file    stm32_eval_spi_sd.c
  * @author  MCD Application Team
  * @version V4.4.0
  * @date    31-December-2010
  * @brief   This file provides a set of functions needed to manage the SPI SD 
  *          Card memory mounted on STM32xx-EVAL board (refer to stm32_eval.h
  *          to know about the boards supporting this memory). 
  *          It implements a high level communication layer for read and write 
  *          from/to this memory. The needed STM32 hardware resources (SPI and 
  *          GPIO) are defined in stm32xx_eval.h file, and the initialization is 
  *          performed in SD_LowLevel_Init() function declared in stm32xx_eval.c 
  *          file.
  *          You can easily tailor this driver to any other development board, 
  *          by just adapting the defines for hardware resources and 
  *          SD_LowLevel_Init() function.
  *            
  *          +-------------------------------------------------------+
  *          |                     Pin assignment                    |
  *          +-------------------------+---------------+-------------+
  *          |  STM32 SPI Pins         |     SD        |    Pin      |
  *          +-------------------------+---------------+-------------+
  *          | SD_SPI_CS_PIN           |   ChipSelect  |    1        |
  *          | SD_SPI_MOSI_PIN / MOSI  |   DataIn      |    2        |
  *          |                         |   GND         |    3 (0 V)  |
  *          |                         |   VDD         |    4 (3.3 V)|
  *          | SD_SPI_SCK_PIN / SCLK   |   Clock       |    5        |
  *          |                         |   GND         |    6 (0 V)  |
  *          | SD_SPI_MISO_PIN / MISO  |   DataOut     |    7        |
  *          +-------------------------+---------------+-------------+
  ******************************************************************************
  * @attention
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2010 STMicroelectronics</center></h2>
  ******************************************************************************  
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32_spi_sd.h"
#include "systick.h"


/* Card type flags (CardType) */
#define CT_MMC		0x01	/* MMC ver 3 */
#define CT_SD1		0x02	/* SD ver 1 */
#define CT_SD2		0x04	/* SD ver 2 */
#define CT_SDC		(CT_SD1|CT_SD2)	/* SD */
#define CT_BLOCK	0x08	/* Block addressing */

#define	INS		(1)	/* Socket: Card is inserted (yes:true, no:false, default:true) */
#define	WP		(0)	/* Socket: Card is write protected (yes:true, no:false, default:false) */


static DSTATUS Stat = STA_NOINIT;	/* Disk status */

static BYTE CardType;		/* b0:MMC, b1:SDv1, b2:SDv2, b3:Block addressing */
static void deselect(void);
static int select(void);
static int wait_ready(void);

static uint8_t SD_WriteByte(uint8_t);
static uint8_t SD_ReadByte(void);

static void rcvr_mmc(uint8_t *, uint32_t);
static void xmit_mmc(const uint8_t *, uint32_t);
static void dly_us(uint32_t);
static void SD_SPI_DeInit(void);
static void SD_SPI_Init(void);
static int rcvr_datablock(BYTE *, UINT);
static int xmit_datablock(const uint8_t * buff, uint8_t token);
static BYTE send_cmd(BYTE cmd, DWORD);

/*-----------------------------------------------------------------------*/
/* Receive bytes from the card (bitbanging)                              */
/*-----------------------------------------------------------------------*/
static void rcvr_mmc(uint8_t * buff,	/* Pointer to read buffer */
		     uint32_t bc	/* Number of bytes to receive */
    )
{
    uint8_t r;

    do {
	r = SD_ReadByte();
	*buff++ = r;		/* Store a received byte */
    } while (--bc);
}

/*-----------------------------------------------------------------------*/
/* Transmit bytes to the card (bitbanging)                               */
/*-----------------------------------------------------------------------*/
static void xmit_mmc(const uint8_t * buff,	/* Data to be sent */
		     uint32_t bc	/* Number of bytes to send */
    )
{
    uint8_t d;


    do {
	d = *buff++;		/* Get a byte to be sent */
	SD_WriteByte(d);
    } while (--bc);
}

/*  Задержка */
static void dly_us(uint32_t n)
{
 delay_ms(n / 100);
#if 0
    do {			/* 9 clocks per loop on avr-gcc -Os */
	asm(" nop ");
    } while (--n);
#endif
}



/**
  * @brief  DeInitializes the SD/SD communication.
  * @param  None
  * @retval None
  */
static void SD_SPI_DeInit(void)
{

    GPIO_InitTypeDef GPIO_InitStructure;


    SPI_Cmd(SD_SPI, DISABLE);	/*!< SD_SPI disable */

    /*!< SD_SPI Periph clock disable */
    RCC_APB2PeriphClockCmd(SD_SPI_CLK, DISABLE);

    /*!< Configure SD_SPI pins: SCK */
    GPIO_InitStructure.GPIO_Pin = SD_SPI_SCK_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(SD_SPI_SCK_GPIO_PORT, &GPIO_InitStructure);

    /*!< Configure SD_SPI pins: MISO */
    GPIO_InitStructure.GPIO_Pin = SD_SPI_MISO_PIN;
    GPIO_Init(SD_SPI_MISO_GPIO_PORT, &GPIO_InitStructure);

    /*!< Configure SD_SPI pins: MOSI */
    GPIO_InitStructure.GPIO_Pin = SD_SPI_MOSI_PIN;
    GPIO_Init(SD_SPI_MOSI_GPIO_PORT, &GPIO_InitStructure);

    /*!< Configure SD_SPI_CS_PIN pin: SD Card CS pin */
    GPIO_InitStructure.GPIO_Pin = SD_SPI_CS_PIN;
    GPIO_Init(SD_SPI_CS_GPIO_PORT, &GPIO_InitStructure);
}


/**
  * @brief  Initializes the SD_SPI and CS pins.
  * @param  None
  * @retval None
  */
static void SD_SPI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef SPI_InitStructure;


    /*Разрешим тактирование SPI1 */
    RCC_APB2PeriphClockCmd(SD_SPI_CLK, ENABLE);

    /* выводы A */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    GPIO_PinAFConfig(SD_SPI_MOSI_GPIO_PORT, SD_SPI_MOSI_GPIO_PIN_SOURCE, GPIO_AF_SPI1);	/*  MOSI */
    GPIO_PinAFConfig(SD_SPI_MISO_GPIO_PORT, SD_SPI_MISO_GPIO_PIN_SOURCE, GPIO_AF_SPI1);	/* MISO */
    GPIO_PinAFConfig(SD_SPI_SCK_GPIO_PORT, SD_SPI_SCK_GPIO_PIN_SOURCE, GPIO_AF_SPI1);	/*!< Configure SD_SPI pins: SCK */


    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;


    /*!< SPI MOSI pin configuration */
    GPIO_InitStructure.GPIO_Pin = SD_SPI_MOSI_PIN;
    GPIO_Init(SD_SPI_MOSI_GPIO_PORT, &GPIO_InitStructure);

    /*!< SPI SCK pin configuration */
    GPIO_InitStructure.GPIO_Pin = SD_SPI_SCK_PIN;
    GPIO_Init(SD_SPI_SCK_GPIO_PORT, &GPIO_InitStructure);

    /*!< SPI MISO pin configuration */
    GPIO_InitStructure.GPIO_Pin = SD_SPI_MISO_PIN;
    GPIO_Init(SD_SPI_MISO_GPIO_PORT, &GPIO_InitStructure);


    /*!< Configure SPI1 Card CS pin in output pushpull mode ******************* */
    GPIO_InitStructure.GPIO_Pin = SD_SPI_CS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(SD_SPI_CS_GPIO_PORT, &GPIO_InitStructure);


    /*!< SD_SPI Config */
    SPI_StructInit(&SPI_InitStructure);
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;

    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;

    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SD_SPI, &SPI_InitStructure);

    /*!< SD_SPI enable */
    SPI_Cmd(SD_SPI, ENABLE);
}



/**
  * @brief  Reads a block of data from the SD.
  * @param  pBuffer: pointer to the buffer that receives the data read from the SD.
  * @param  ReadAddr: SD's internal address to read from.
  * @param  BlockSize: the SD card Data block size.
  *
  * Receive a data packet from the card 
 */
static int rcvr_datablock(	/* 1:OK, 0:Failed */
			     BYTE * buff,	/* Data buffer to store received data */
			     UINT btr	/* Byte count */
    )
{
    BYTE d[2];
    UINT tmr;


    for (tmr = 1000; tmr; tmr--) {	/* Wait for data packet in timeout of 100ms */
	rcvr_mmc(d, 1);
	if (d[0] != 0xFF)
	    break;
	dly_us(100);
    }
    if (d[0] != 0xFE)
	return 0;		/* If not valid data token, return with error */

    rcvr_mmc(buff, btr);	/* Receive the data block into buffer */
    rcvr_mmc(d, 2);		/* Discard CRC */

    return 1;			/* Return with success */
}



/*-----------------------------------------------------------------------*/
/* Send a data packet to the card                                        */
/*-----------------------------------------------------------------------*/
static int xmit_datablock(	/* 1:OK, 0:Failed */
			     const uint8_t * buff,	/* 512 byte data block to be transmitted */
			     uint8_t token	/* Data/Stop token */
    )
{
    BYTE d[2];

    if (!wait_ready())
	return 0;

    d[0] = token;
    xmit_mmc(d, 1);		/* Xmit a token */
    if (token != 0xFD) {	/* Is it data token? */
	xmit_mmc(buff, 512);	/* Xmit the 512 byte data block to MMC */
	rcvr_mmc(d, 2);		/* Xmit dummy CRC (0xFF,0xFF) */
	rcvr_mmc(d, 1);		/* Receive data response */
	if ((d[0] & 0x1F) != 0x05)	/* If not accepted, return with error */
	    return 0;
    }

    return 1;
}




/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/
static int wait_ready(void)
{				/* 1:OK, 0:Timeout */
    uint8_t d;
    uint32_t tmr;


    for (tmr = 5000; tmr; tmr--) {	/* Wait for ready in timeout of 500ms */
	d = SD_ReadByte();
	if (d == 0xFF)
	    break;
	dly_us(100);
    }

    return tmr ? 1 : 0;
}

/*-----------------------------------------------------------------------*/
/* Deselect the card and release SPI bus                                 */
/*-----------------------------------------------------------------------*/
static void deselect(void)
{
    SD_CS_HIGH();
    SD_ReadByte();		/* Dummy clock (force DO hi-z for multiple slave SPI) */
}

/*-----------------------------------------------------------------------*/
/* Select the card and wait for ready                                    */
/*-----------------------------------------------------------------------*/
static int select(void)
{				/* 1:Successful, 0:Timeout */
    SD_CS_LOW();
    SD_ReadByte();		/* Dummy clock (force DO enabled) */

    if (wait_ready())
	return 1;		/* OK */
    deselect();
    return 0;			/* Timeout */
}



/**
  * @brief  Send 5 bytes command to the SD card.
  * @param  Cmd: The user expected command to send to SD card.
  * @param  Arg: The command argument.
  * @param  Crc: The CRC.
  * @retval None
  */
static BYTE send_cmd(		/* Returns command response (bit7==1:Send failed) */
			BYTE Cmd,	/* Command byte */
			DWORD Arg)
{				/* Argument */
    uint8_t i = 0x00, d, n;
    uint8_t Frame[6];

    if (Cmd & 0x80) {		/* ACMD<n> is the command sequense of CMD55-CMD<n> */
	Cmd &= 0x7F;
	n = send_cmd(SD_CMD55, 0);
	if (n > 1)
	    return n;
    }

    /* Select the card and wait for ready */
    deselect();
    if (!select())
	return 0xFF;


    Frame[0] = (Cmd | 0x40);	/*!< Construct byte 1 */
    Frame[1] = (uint8_t) (Arg >> 24);	/*!< Construct byte 2 */
    Frame[2] = (uint8_t) (Arg >> 16);	/*!< Construct byte 3 */
    Frame[3] = (uint8_t) (Arg >> 8);	/*!< Construct byte 4 */
    Frame[4] = (uint8_t) (Arg);	/*!< Construct byte 5 */

    n = 0x01;			/* Dummy CRC + Stop */
    if (Cmd == SD_CMD_GO_IDLE_STATE)
	n = 0x95;		/* (valid CRC for CMD0(0)) */
    if (Cmd == SD_CMD_SEND_IF_COND)
	n = 0x87;		/* (valid CRC for CMD8(0x1AA)) */
    Frame[5] = n;

    for (i = 0; i < 6; i++) {
	SD_WriteByte(Frame[i]);	/*!< Send the Cmd bytes */
    }

    /* Receive command response */
    if (Cmd == SD_CMD_STOP_TRANSMISSION)
	d = SD_ReadByte();	/* Skip a stuff byte when stop reading */
    i = 10;			/* Wait for a valid response in timeout of 10 attempts */
    do
	d = SD_ReadByte();
    while ((d & 0x80) && --i);

    return d;			/* Return with the response value */
}



/**
  * @brief  Write a byte on the SD.
  * @param  Data: byte to send.
  * @retval None
  */
static uint8_t SD_WriteByte(uint8_t Data)
{
    /*!< Wait until the transmit buffer is empty */
    while (SPI_I2S_GetFlagStatus(SD_SPI, SPI_I2S_FLAG_TXE) == RESET) {
    }

    /*!< Send the byte */
    SPI_I2S_SendData(SD_SPI, Data);

    /*!< Wait to receive a byte */
    while (SPI_I2S_GetFlagStatus(SD_SPI, SPI_I2S_FLAG_RXNE) == RESET) {
    }

    /*!< Return the byte read from the SPI bus */
    return SPI_I2S_ReceiveData(SD_SPI);
}

/**
  * @brief  Read a byte from the SD.
  * @param  None
  * @retval The received byte.
  */
static uint8_t SD_ReadByte(void)
{
    uint8_t Data = 0;

    /*!< Wait until the transmit buffer is empty */
    while (SPI_I2S_GetFlagStatus(SD_SPI, SPI_I2S_FLAG_TXE) == RESET) {
    }
    /*!< Send the byte */
    SPI_I2S_SendData(SD_SPI, SD_DUMMY_BYTE);

    /*!< Wait until a data is received */
    while (SPI_I2S_GetFlagStatus(SD_SPI, SPI_I2S_FLAG_RXNE) == RESET) {
    }
    /*!< Get the received data */
    Data = SPI_I2S_ReceiveData(SD_SPI);

    /*!< Return the shifted data */
    return Data;
}

/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE drv)
{				/* Drive number (always 0) */

    DSTATUS s = Stat;
    BYTE ocr[4];


    if (drv || !INS) {
	s = STA_NODISK | STA_NOINIT;
    } else {
	s &= ~STA_NODISK;
	if (WP)			/* Check card write protection */
	    s |= STA_PROTECT;
	else
	    s &= ~STA_PROTECT;
	if (!(s & STA_NOINIT)) {
	    if (send_cmd(SD_CMD58, 0))	/* Check if the card is kept initialized */
		s |= STA_NOINIT;
	    rcvr_mmc(ocr, 4);
	    SD_CS_HIGH();
	}
    }
    Stat = s;

    return s;
}



/* Put SD in SPI mode */
DSTATUS disk_initialize(BYTE drv)
{				/* Physical drive nmuber (0) */
    uint8_t n, ty, cmd, buf[4];
    uint32_t tmr;
    DSTATUS s;


    SD_SPI_Init();


    s = disk_status(drv);	/* Check if card is in the socket */
    if (s & STA_NODISK)
	return s;

    SD_CS_HIGH();
    for (n = 10; n; n--)
	rcvr_mmc(buf, 1);	/* 80 dummy clocks */

    ty = 0;
    if (send_cmd(SD_CMD_GO_IDLE_STATE, 0) == 1) {	/* Enter Idle state */
	if (send_cmd(SD_CMD_SEND_IF_COND, 0x1AA) == 1) {	/* SDv2? */

	    rcvr_mmc(buf, 4);	/* Get trailing return value of R7 resp */

	    if (buf[2] == 0x01 && buf[3] == 0xAA) {	/* The card can work at vdd range of 2.7-3.6V */
		for (tmr = 1000; tmr; tmr--) {	/* Wait for leaving idle state (ACMD41 with HCS bit) */
		    if (send_cmd(SD_ACMD41, 1UL << 30) == 0)
			break;
		    dly_us(1000);
		}
		if (tmr && send_cmd(SD_CMD58, 0) == 0) {	/* Check CCS bit in the OCR */
		    rcvr_mmc(buf, 4);
		    ty = (buf[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;	/* SDv2 */
		}
	    }
	} else {		/* SDv1 or MMCv3 */
	    if (send_cmd(SD_ACMD41, 0) <= 1) {
		ty = CT_SD1;
		cmd = SD_ACMD41;	/* SDv1 */
	    } else {
		ty = CT_MMC;
		cmd = SD_CMD_SEND_OP_COND;	/* MMCv3 */
	    }
	    for (tmr = 1000; tmr; tmr--) {	/* Wait for leaving idle state */
		if (send_cmd(cmd, 0) == 0)
		    break;
		dly_us(1000);
	    }
	    if (!tmr || send_cmd(SD_CMD_SET_BLOCKLEN, 512) != 0)	/* Set R/W block length to 512 */
		ty = 0;
	}
    }
    CardType = ty;
    if (ty)			/* Initialization succeded */
	s &= ~STA_NOINIT;
    else			/* Initialization failed */
	s |= STA_NOINIT;
    Stat = s;



#if 0
    print_string("SD карта типа ");
    if (ty & CT_SD1)
	print_string("SD1 ");

    if (ty & CT_SD2)
	print_string("SD2 ");

    if (ty & CT_MMC)
	print_string("CT_MMC ");

    if (ty & CT_BLOCK)
	print_string("CT_BLOCK ");

    print_string("\r\n");
#endif



    deselect();
    return s;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE drv,	/* Physical drive nmuber (0) */
		  BYTE * buff,	/* Pointer to the data buffer to store read data */
		  DWORD sector,	/* Start sector number (LBA) */
		  BYTE count	/* Sector count (1..128) */
    )
{
    DSTATUS s;


    s = disk_status(drv);
    if (s & STA_NOINIT)
	return RES_NOTRDY;
    if (!count)
	return RES_PARERR;
    if (!(CardType & CT_BLOCK))
	sector *= 512;		/* Convert LBA to byte address if needed */

    if (count == 1) {		/* Single block read */
	if ((send_cmd(SD_CMD_READ_SINGLE_BLOCK, sector) == 0)	/* READ_SINGLE_BLOCK */
	    &&rcvr_datablock(buff, 512))
	    count = 0;
    } else {			/* Multiple block read */
	if (send_cmd(SD_CMD_READ_MULT_BLOCK, sector) == 0) {	/* READ_MULTIPLE_BLOCK */
	    do {
		if (!rcvr_datablock(buff, 512))
		    break;
		buff += 512;
	    } while (--count);
	    send_cmd(SD_CMD_STOP_TRANSMISSION, 0);	/* STOP_TRANSMISSION */
	}
    }
    deselect();

    return count ? RES_ERROR : RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
DRESULT disk_write(BYTE drv,	/* Physical drive nmuber (0) */
		   const BYTE * buff,	/* Pointer to the data to be written */
		   DWORD sector,	/* Start sector number (LBA) */
		   BYTE count	/* Sector count (1..128) */
    )
{
    DSTATUS s;


    s = disk_status(drv);
    if (s & STA_NOINIT)
	return RES_NOTRDY;
    if (s & STA_PROTECT)
	return RES_WRPRT;
    if (!count)
	return RES_PARERR;
    if (!(CardType & CT_BLOCK))
	sector *= 512;		/* Convert LBA to byte address if needed */

    if (count == 1) {		/* Single block write */
	if ((send_cmd(SD_CMD_WRITE_SINGLE_BLOCK, sector) == 0)	/* WRITE_BLOCK */
	    &&xmit_datablock(buff, 0xFE))
	    count = 0;
    } else {			/* Multiple block write */
	if (CardType & CT_SDC)
	    send_cmd(SD_ACMD23, count);
	if (send_cmd(SD_CMD_WRITE_MULT_BLOCK, sector) == 0) {	/* WRITE_MULTIPLE_BLOCK */
	    do {
		if (!xmit_datablock(buff, 0xFC))
		    break;
		buff += 512;
	    } while (--count);
	    if (!xmit_datablock(0, 0xFD))	/* STOP_TRAN token */
		count = 1;
	}
    }
    deselect();

    return count ? RES_ERROR : RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(BYTE drv,	/* Physical drive nmuber (0) */
		   BYTE ctrl,	/* Control code */
		   void *buff	/* Buffer to send/receive control data */
    )
{
    DRESULT res;
    BYTE n, csd[16];
    WORD cs;


    if (disk_status(drv) & STA_NOINIT)	/* Check if card is in the socket */
	return RES_NOTRDY;

    res = RES_ERROR;
    switch (ctrl) {
    case CTRL_SYNC:		/* Make sure that no pending write process */
	if (select()) {
	    deselect();
	    res = RES_OK;
	}
	break;

    case GET_SECTOR_COUNT:	/* Get number of sectors on the disk (DWORD) */
	if ((send_cmd(SD_CMD_SEND_CSD, 0) == 0) && rcvr_datablock(csd, 16)) {
	    if ((csd[0] >> 6) == 1) {	/* SDC ver 2.00 */
		cs = csd[9] + ((WORD) csd[8] << 8) + 1;
		*(DWORD *) buff = (DWORD) cs << 10;
	    } else {		/* SDC ver 1.XX or MMC */
		n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
		cs = (csd[8] >> 6) + ((WORD) csd[7] << 2) + ((WORD) (csd[6] & 3) << 10) + 1;
		*(DWORD *) buff = (DWORD) cs << (n - 9);
	    }
	    res = RES_OK;
	}
	break;

    case GET_BLOCK_SIZE:	/* Get erase block size in unit of sector (DWORD) */
	*(DWORD *) buff = 128;
	res = RES_OK;
	break;

    default:
	res = RES_PARERR;
    }

    deselect();

    return res;
}

/*---------------------------------------------------------*/
/* User Provided Timer Function for FatFs module           */
/*---------------------------------------------------------*/
DWORD get_fattime(void)
{
    return ((DWORD) (2010 - 1980) << 25)	/* Fixed to Jan. 1, 2010 */
	|((DWORD) 1 << 21)
	| ((DWORD) 1 << 16)
	| ((DWORD) 0 << 11)
	| ((DWORD) 0 << 5)
	| ((DWORD) 0 >> 1);
}
