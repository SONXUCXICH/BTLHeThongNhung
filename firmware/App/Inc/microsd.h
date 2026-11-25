#ifndef MICROSD_H
#define MICROSD_H

#include "main.h"

#define SD_SECTOR_SIZE 512U

typedef enum
{
    SD_OK = 0,
    SD_ERROR,
    SD_TIMEOUT,
    SD_NOT_READY
} SD_Status;

typedef enum
{
    SD_CARD_UNKNOWN = 0,
    SD_CARD_SDSC,
    SD_CARD_SDHC
} SD_CardType;

/* Debug variables */
extern volatile uint8_t sd_response;
extern volatile uint8_t sd_test_ok;
extern volatile uint8_t sd_initialized;
extern volatile SD_CardType sd_card_type;

/* Public functions */
uint8_t MicroSD_Test(void);

SD_Status MicroSD_Init(void);

SD_Status MicroSD_ReadBlock(
    uint32_t sector,
    uint8_t *buffer
);

SD_Status MicroSD_WriteBlock(
    uint32_t sector,
    const uint8_t *buffer
);

SD_Status MicroSD_Sync(void);

#endif