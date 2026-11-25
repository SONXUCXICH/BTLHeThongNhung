#include "microsd.h"

extern SPI_HandleTypeDef hspi2;

volatile uint8_t sd_response = 0xFF;
volatile uint8_t sd_test_ok = 0;
volatile uint8_t sd_initialized = 0;
volatile SD_CardType sd_card_type = SD_CARD_UNKNOWN;

static uint8_t spi_error = 0;

/* SPI transfer */
static uint8_t SD_Transfer(uint8_t data)
{
    uint8_t rx = 0xFF;

    if (HAL_SPI_TransmitReceive(
        &hspi2, &data, &rx, 1, 100) != HAL_OK)
    {
        spi_error = 1;
        return 0xFF;
    }

    return rx;
}

/* Chip select */
static void SD_Select(void)
{
    HAL_GPIO_WritePin(SD_CS_GPIO_Port,
                      SD_CS_Pin, GPIO_PIN_RESET);
}

/* Chip deselect */
static void SD_Deselect(void)
{
    HAL_GPIO_WritePin(SD_CS_GPIO_Port,
                      SD_CS_Pin, GPIO_PIN_SET);

    SD_Transfer(0xFF);
}

/* Wait until card is ready */
static uint8_t SD_WaitReady(uint32_t timeout)
{
    uint32_t start = HAL_GetTick();

    do
    {
        uint8_t response = SD_Transfer(0xFF);

        if (spi_error)
            return 0;

        if (response == 0xFF)
            return 1;

    } while ((HAL_GetTick() - start) < timeout);

    return 0;
}

/* Send SD command */
static uint8_t SD_Command(uint8_t cmd,
                          uint32_t arg,
                          uint8_t crc)
{
    uint8_t response = 0xFF;

    SD_Deselect();
    SD_Select();

    if (!SD_WaitReady(500))
        return 0xFF;

    SD_Transfer(0x40 | cmd);
    SD_Transfer((uint8_t)(arg >> 24));
    SD_Transfer((uint8_t)(arg >> 16));
    SD_Transfer((uint8_t)(arg >> 8));
    SD_Transfer((uint8_t)arg);
    SD_Transfer(crc);

    for (uint8_t i = 0; i < 10; i++)
    {
        response = SD_Transfer(0xFF);

        if ((response & 0x80) == 0)
            break;
    }

    sd_response = response;

    return response;
}

/* Initialize SD card */
SD_Status MicroSD_Init(void)
{
    uint8_t response;
    uint8_t r7[4];
    uint8_t ocr[4];
    uint8_t version2 = 0;
    uint32_t start;

    spi_error = 0;
    sd_initialized = 0;
    sd_test_ok = 0;
    sd_card_type = SD_CARD_UNKNOWN;

    SD_Deselect();
    HAL_Delay(100);

    /* At least 74 clock cycles */
    for (uint8_t i = 0; i < 10; i++)
        SD_Transfer(0xFF);

    /* CMD0: enter idle state */
    start = HAL_GetTick();

    do
    {
        response = SD_Command(0, 0, 0x95);
        SD_Deselect();

        if (response == 0x01)
            break;

    } while (!spi_error &&
             (HAL_GetTick() - start) < 1000);

    if (response != 0x01 || spi_error)
        goto fail;

    sd_test_ok = 1;

    /* CMD8: check SD version */
    response = SD_Command(8, 0x1AA, 0x87);

    if (response == 0x01)
    {
        for (uint8_t i = 0; i < 4; i++)
            r7[i] = SD_Transfer(0xFF);

        if (r7[2] != 0x01 || r7[3] != 0xAA)
            goto fail;

        version2 = 1;
    }
    else if (response != 0x05)
    {
        goto fail;
    }

    SD_Deselect();

    /* CMD55 + ACMD41 */
    start = HAL_GetTick();

    do
    {
        response = SD_Command(55, 0, 0x01);
        SD_Deselect();

        if (response > 0x01)
            goto fail;

        response = SD_Command(
            41,
            version2 ? 0x40000000UL : 0,
            0x01
        );

        SD_Deselect();

        if (response == 0x00)
            break;

        if (response != 0x01)
            goto fail;

        HAL_Delay(10);

    } while (!spi_error &&
             (HAL_GetTick() - start) < 2000);

    if (response != 0x00 || spi_error)
        goto fail;

    /* CMD58: read OCR */
    response = SD_Command(58, 0, 0x01);

    if (response != 0x00)
        goto fail;

    for (uint8_t i = 0; i < 4; i++)
        ocr[i] = SD_Transfer(0xFF);

    SD_Deselect();

    if ((ocr[0] & 0x80) == 0)
        goto fail;

    if (version2 && (ocr[0] & 0x40))
    {
        sd_card_type = SD_CARD_SDHC;
    }
    else
    {
        sd_card_type = SD_CARD_SDSC;

        /* CMD16: set block length */
        response = SD_Command(
            16, SD_SECTOR_SIZE, 0x01
        );

        SD_Deselect();

        if (response != 0x00)
            goto fail;
    }

    if (spi_error)
        goto fail;

    sd_initialized = 1;

    return SD_OK;

fail:
    SD_Deselect();
    sd_initialized = 0;
    sd_card_type = SD_CARD_UNKNOWN;

    return SD_ERROR;
}

/* Test card initialization */
uint8_t MicroSD_Test(void)
{
    return MicroSD_Init() == SD_OK;
}

/* Convert sector to card address */
static uint32_t SD_Address(uint32_t sector)
{
    if (sd_card_type == SD_CARD_SDSC)
        return sector * SD_SECTOR_SIZE;

    return sector;
}

/* Read one 512-byte sector */
SD_Status MicroSD_ReadBlock(uint32_t sector,
                            uint8_t *buffer)
{
    uint8_t token;
    uint32_t start;
    SD_Status status = SD_ERROR;

    if (!sd_initialized)
        return SD_NOT_READY;

    if (buffer == NULL)
        return SD_ERROR;

    if (sd_card_type == SD_CARD_SDSC &&
        sector > UINT32_MAX / SD_SECTOR_SIZE)
        return SD_ERROR;

    spi_error = 0;

    if (SD_Command(17, SD_Address(sector), 0x01)
        != 0x00)
        goto done;

    start = HAL_GetTick();

    do
    {
        token = SD_Transfer(0xFF);

        if (spi_error)
            goto done;

        if (token == 0xFE)
            break;

        if (token != 0xFF)
            goto done;

    } while ((HAL_GetTick() - start) < 500);

    if (token != 0xFE)
    {
        status = SD_TIMEOUT;
        goto done;
    }

    for (uint16_t i = 0; i < SD_SECTOR_SIZE; i++)
        buffer[i] = SD_Transfer(0xFF);

    /* Discard two CRC bytes */
    SD_Transfer(0xFF);
    SD_Transfer(0xFF);

    if (!spi_error)
        status = SD_OK;

done:
    SD_Deselect();

    return spi_error ? SD_ERROR : status;
}

/* Write one 512-byte sector */
SD_Status MicroSD_WriteBlock(uint32_t sector,
                             const uint8_t *buffer)
{
    uint8_t response;
    SD_Status status = SD_ERROR;

    if (!sd_initialized)
        return SD_NOT_READY;

    if (buffer == NULL)
        return SD_ERROR;

    if (sd_card_type == SD_CARD_SDSC &&
        sector > UINT32_MAX / SD_SECTOR_SIZE)
        return SD_ERROR;

    spi_error = 0;

    if (SD_Command(24, SD_Address(sector), 0x01)
        != 0x00)
        goto done;

    if (!SD_WaitReady(500))
    {
        status = SD_TIMEOUT;
        goto done;
    }

    /* Start block token */
    SD_Transfer(0xFE);

    for (uint16_t i = 0; i < SD_SECTOR_SIZE; i++)
        SD_Transfer(buffer[i]);

    /* Dummy CRC */
    SD_Transfer(0xFF);
    SD_Transfer(0xFF);

    response = SD_Transfer(0xFF);

    if ((response & 0x1F) != 0x05)
        goto done;

    if (!SD_WaitReady(1000))
    {
        status = SD_TIMEOUT;
        goto done;
    }

    if (!spi_error)
        status = SD_OK;

done:
    SD_Deselect();

    return spi_error ? SD_ERROR : status;
}

/* Wait for pending write operation */
SD_Status MicroSD_Sync(void)
{
    uint8_t ready;

    if (!sd_initialized)
        return SD_NOT_READY;

    spi_error = 0;

    SD_Select();
    ready = SD_WaitReady(1000);
    SD_Deselect();

    if (spi_error)
        return SD_ERROR;

    return ready ? SD_OK : SD_TIMEOUT;
}