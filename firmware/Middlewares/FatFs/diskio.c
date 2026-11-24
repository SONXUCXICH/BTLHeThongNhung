#include "ff.h"
#include "diskio.h"
#include "microsd.h"

#define DEV_SD 0

/* Check SD card status */
DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != DEV_SD)
        return STA_NOINIT;

    return sd_initialized ? 0 : STA_NOINIT;
}

/* Initialize SD card */
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != DEV_SD)
        return STA_NOINIT;

    if (MicroSD_Init() == SD_OK)
        return 0;

    return STA_NOINIT;
}

/* Read sectors */
DRESULT disk_read(BYTE pdrv,
                  BYTE *buff,
                  LBA_t sector,
                  UINT count)
{
    if (pdrv != DEV_SD || !buff || count == 0)
        return RES_PARERR;

    if (!sd_initialized)
        return RES_NOTRDY;

    while (count--)
    {
        if (MicroSD_ReadBlock(
            (uint32_t)sector, buff) != SD_OK)
        {
            return RES_ERROR;
        }

        sector++;
        buff += SD_SECTOR_SIZE;
    }

    return RES_OK;
}

/* Write sectors */
#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv,
                   const BYTE *buff,
                   LBA_t sector,
                   UINT count)
{
    if (pdrv != DEV_SD || !buff || count == 0)
        return RES_PARERR;

    if (!sd_initialized)
        return RES_NOTRDY;

    while (count--)
    {
        if (MicroSD_WriteBlock(
            (uint32_t)sector, buff) != SD_OK)
        {
            return RES_ERROR;
        }

        sector++;
        buff += SD_SECTOR_SIZE;
    }

    return RES_OK;
}

#endif

/* Control SD card */
DRESULT disk_ioctl(BYTE pdrv,
                   BYTE cmd,
                   void *buff)
{
    if (pdrv != DEV_SD)
        return RES_PARERR;

    if (!sd_initialized)
        return RES_NOTRDY;

    switch (cmd)
    {
        case CTRL_SYNC:
            return (MicroSD_Sync() == SD_OK)
                   ? RES_OK : RES_ERROR;

        case GET_SECTOR_SIZE:
            if (!buff)
                return RES_PARERR;

            *(WORD *)buff = SD_SECTOR_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
            if (!buff)
                return RES_PARERR;

            *(DWORD *)buff = 1;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}