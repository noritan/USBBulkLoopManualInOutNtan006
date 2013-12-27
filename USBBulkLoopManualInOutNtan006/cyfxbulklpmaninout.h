/*
 ## Cypress USB 3.0 Platform header file (cyfxbulklpmaninout.h)
 ## ===========================
 ##
 ##  Copyright Cypress Semiconductor Corporation, 2010-2011,
 ##  All Rights Reserved
 ##  UNPUBLISHED, LICENSED SOFTWARE.
 ##
 ##  CONFIDENTIAL AND PROPRIETARY INFORMATION
 ##  WHICH IS THE PROPERTY OF CYPRESS.
 ##
 ##  Use of this file is governed
 ##  by the license agreement included in the file
 ##
 ##     <install>/license/license.txt
 ##
 ##  where <install> is the Cypress software
 ##  installation root directory path.
 ##
 ## ===========================
*/

/* This file contains the constants used by the Bulk Loop application example */

#ifndef _INCLUDED_CYFXBULKLPMANINOUT_H_
#define _INCLUDED_CYFXBULKLPMANINOUT_H_

#include "cyu3types.h"
#include "cyu3usbconst.h"
#include "cyu3externcstart.h"

#define CY_FX_BULKLP_DMA_BUF_SIZE       (20*1024)       // Maximum SPI packet data size
#define CY_FX_BULKLP_DMA_BUF_COUNT      (1)             // DMA channel buffer count
#define CY_FX_BULKLP_DMA_TX_SIZE        (0)                       /* DMA transfer size is set to infinite */
#define CY_FX_BULKLP_THREAD_STACK       (0x1000)                  /* Bulk loop application thread stack size */
#define CY_FX_BULKLP_THREAD_PRIORITY    (8)                       /* Bulk loop application thread priority */

#define CY_FX_SECTOR_SIZE               (CY_FX_BULKLP_DMA_BUF_SIZE+32)  // Sector size
#define CY_FX_N_SECTORS                 (256*1024/CY_FX_SECTOR_SIZE)    // Number of sectors in 2Mbit FRAM

/* USB vendor requests supported by the application. */

/* USB vendor request to initialize WRITE to SPI FRAM. Any bytes of data
 * can be written to the FRAM at a sector specified by the wIndex parameter.
 * The maximum allowed data size is 20kBytes.  The data size is specified
 * by the BULK data size following this request.
 */
#define CY_FX_RQT_FRAM_WRITE            (0xC2)

/* USB vendor request to initialize READ from SPI FRAM.  Any bytes of data
 * can be read from the FRAM at a sector specified by the wIndex parameter.
 * The maximum allowed data size is 20kBytes.  The data size read from FRAM
 * is specified by the wValue parameter.  The data is read be a BULK IN
 * transfer following this request.
 */
#define CY_FX_RQT_FRAM_READ             (0xC3)

/* Endpoint and socket definitions for the bulkloop application */

/* To change the producer and consumer EP enter the appropriate EP numbers for the #defines.
 * In the case of IN endpoints enter EP number along with the direction bit.
 * For eg. EP 6 IN endpoint is 0x86
 *     and EP 6 OUT endpoint is 0x06.
 * To change sockets mention the appropriate socket number in the #defines. */

/* Note: For USB 2.0 the endpoints and corresponding sockets are one-to-one mapped
         i.e. EP 1 is mapped to UIB socket 1 and EP 2 to socket 2 so on */

#define CY_FX_EP_PRODUCER               0x01    /* EP 1 OUT */
#define CY_FX_EP_CONSUMER               0x81    /* EP 1 IN */

#define CY_FX_EP_PRODUCER_SOCKET        CY_U3P_UIB_SOCKET_PROD_1    /* Socket 1 is producer */
#define CY_FX_EP_CONSUMER_SOCKET        CY_U3P_UIB_SOCKET_CONS_1    /* Socket 1 is consumer */

/* Extern definitions for the USB Descriptors */
extern const uint8_t CyFxUSB20DeviceDscr[];
extern const uint8_t CyFxUSB30DeviceDscr[];
extern const uint8_t CyFxUSBDeviceQualDscr[];
extern const uint8_t CyFxUSBFSConfigDscr[];
extern const uint8_t CyFxUSBHSConfigDscr[];
extern const uint8_t CyFxUSBBOSDscr[];
extern const uint8_t CyFxUSBSSConfigDscr[];
extern const uint8_t CyFxUSBStringLangIDDscr[];
extern const uint8_t CyFxUSBManufactureDscr[];
extern const uint8_t CyFxUSBProductDscr[];

#include "cyu3externcend.h"

#endif /* _INCLUDED_CYFXBULKLPMANINOUT_H_ */

/*[]*/
