/*
 ## Cypress USB 3.0 Platform source file (cyfxbulklpmaninout.c)
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

/* This file illustrates the bulkloop application example using the DMA MANUAL_IN and DMA MANUAL_OUT mode. */

/*
   This examples illustrate a loopback mechanism between two USB bulk endpoints. The example comprises of
   vendor class USB enumeration descriptors with two Bulk Endpoints. A bulk OUT endpoint acts as the producer
   of data from the host. A bulk IN endpoint acts as the consumer of data to the host. The loopback is achieved
   with the help of a DMA MANUAL IN channel and a DMA MANUAL_OUT channel. A DMA MANUAL_IN channel is created 
   between the producer USB bulk endpoint and the CPU. A DMA MANUAL_OUT channel is created between the CPU
   and the consumer USB bulk endpoint.

   Data is received in the IN Channel DMA buffer from the host through the producer endpoint. CPU waits for data
   from this channel and then copies the contents of the IN channel DMA buffer into the OUT channel DMA buffer.
   The CPU issues commit of the DMA data transfer to the consumer endpoint which then gets transferred to the host.
   This example does not make use of callbacks to commit the data into the channel. It uses the GetBuffer calls
   to wait for buffer from the application thread. Refer to the MANUAL mode example for how to commit data from
   callback.

   The DMA buffer size for each channel is defined based on the USB speed. 64 for full speed, 512 for high speed
   and 1024 for super speed. CY_FX_BULKLP_DMA_BUF_COUNT in the header file defines the number of DMA buffers per
   channel.
 */

#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyfxbulklpmaninout.h"
#include "cyu3usb.h"
#include "cyu3uart.h"
#include "cyu3spi.h"
CyU3PThread     BulkLpAppThread;	 /* Bulk loop application thread structure */
CyU3PDmaChannel glChHandleBulkLpIn;      /* DMA MANUAL_IN channel handle.          */
CyU3PDmaChannel glChHandleBulkLpOut;     /* DMA MANUAL_OUT channel handle.         */
CyU3PDmaChannel glSpiTxHandle;          // SPI Tx channel handle
CyU3PDmaChannel glSpiRxHandle;          // SPI Rx channel handle

CyBool_t glIsApplnActive = CyFalse;      /* Whether the loopback application is active or not. */

uint16_t    glSectorToWrite;            // Sector number to be WRITTEN
uint16_t    glSectorToRead;             // Sector number to be READ
uint16_t    glSizeToRead;               // Data size to be READ
CyU3PEvent  glFramEvent;                // Event group used to signal the thread a READ/WRITE request.
uint32_t    glPacketSize;               // Current packet size

/* Application Error Handler */
void
CyFxAppErrorHandler (
        CyU3PReturnStatus_t apiRetStatus    /* API return status */
        )
{
    /* Application failed with the error code apiRetStatus */

    /* Add custom debug or recovery actions here */

    /* Loop Indefinitely */
    for (;;)
    {
        /* Thread sleep : 100 ms */
        CyU3PThreadSleep (100);
    }
}

/* This function initializes the debug module. The debug prints
 * are routed to the UART and can be seen using a UART console
 * running at 115200 baud rate. */
void
CyFxBulkLpApplnDebugInit (void)
{
    CyU3PUartConfig_t uartConfig;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Initialize the UART for printing debug messages */
    apiRetStatus = CyU3PUartInit();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error handling */
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set UART configuration */
    CyU3PMemSet ((uint8_t *)&uartConfig, 0, sizeof (uartConfig));
    uartConfig.baudRate = CY_U3P_UART_BAUDRATE_115200;
    uartConfig.stopBit = CY_U3P_UART_ONE_STOP_BIT;
    uartConfig.parity = CY_U3P_UART_NO_PARITY;
    uartConfig.txEnable = CyTrue;
    uartConfig.rxEnable = CyFalse;
    uartConfig.flowCtrl = CyFalse;
    uartConfig.isDma = CyTrue;

    apiRetStatus = CyU3PUartSetConfig (&uartConfig, NULL);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set the UART transfer to a really large value. */
    apiRetStatus = CyU3PUartTxSetBlockXfer (0xFFFFFFFF);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Initialize the debug module. */
    apiRetStatus = CyU3PDebugInit (CY_U3P_LPP_SOCKET_UART_CONS, 8);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }
}

/*
 * SPI initialization for FRAM programmer application.
 */
CyU3PReturnStatus_t
CyFxBulkLpSpiInit (void)
{
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
    CyU3PSpiConfig_t spiConfig;
    CyU3PDmaChannelConfig_t dmaConfig;

    /* Start the SPI module and configure the master. */
    status = CyU3PSpiInit();
    if (status != CY_U3P_SUCCESS)
    {
        return status;
    }

    /*
     * Start the SPI master block. Run the SPI clock at 24MHz
     * and configure the word length to 8 bits. Also configure
     * the slave select using the firmware.
     */
    CyU3PMemSet ((uint8_t *)&spiConfig, 0, sizeof(spiConfig));
    spiConfig.isLsbFirst = CyFalse;
    spiConfig.cpol       = CyTrue;
    spiConfig.ssnPol     = CyFalse;
    spiConfig.cpha       = CyTrue;
    spiConfig.leadTime   = CY_U3P_SPI_SSN_LAG_LEAD_HALF_CLK;
    spiConfig.lagTime    = CY_U3P_SPI_SSN_LAG_LEAD_HALF_CLK;
    spiConfig.ssnCtrl    = CY_U3P_SPI_SSN_CTRL_FW;
    spiConfig.clock      = 24000000;
    spiConfig.wordLen    = 8;

    status = CyU3PSpiSetConfig (&spiConfig, NULL);
    if (status != CY_U3P_SUCCESS)
    {
        return status;
    }

    /* Create the DMA channels for SPI write and read. */
    CyU3PMemSet ((uint8_t *)&dmaConfig, 0, sizeof(dmaConfig));
    dmaConfig.size           = CY_FX_BULKLP_DMA_BUF_SIZE;
    /* No buffers need to be allocated as this channel
     * will be used only in override mode. */
    dmaConfig.count          = 0;
    dmaConfig.prodAvailCount = 0;
    dmaConfig.dmaMode        = CY_U3P_DMA_MODE_BYTE;
    dmaConfig.prodHeader     = 0;
    dmaConfig.prodFooter     = 0;
    dmaConfig.consHeader     = 0;
    dmaConfig.notification   = 0;
    dmaConfig.cb             = NULL;

    /* Channel to write to SPI flash. */
    dmaConfig.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    dmaConfig.consSckId = CY_U3P_LPP_SOCKET_SPI_CONS;
    status = CyU3PDmaChannelCreate (&glSpiTxHandle,
            CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaConfig);
    if (status != CY_U3P_SUCCESS)
    {
        return status;
    }

    /* Channel to read from SPI flash. */
    dmaConfig.prodSckId = CY_U3P_LPP_SOCKET_SPI_PROD;
    dmaConfig.consSckId = CY_U3P_CPU_SOCKET_CONS;
    status = CyU3PDmaChannelCreate (&glSpiRxHandle,
            CY_U3P_DMA_TYPE_MANUAL_IN, &dmaConfig);

    return status;
}

/*
 * Read a data from a specified sector
 *
 * Parameters
 *
 * uint16_t sector
 *     The sector number where the data to be read is located.
 *     The maximum number is specified in the header file.
 *     No sector number validation is implemented in this function.
 * uint8_t *buffer
 *     Buffer address where the read data is to be stored.
 *     The buffer should be a 32 byte aligned address because the
 *     value is directly used for the DMA channel buffer.
 * uint16_t byteCount
 *     The number of bytes to be read from the SPI FRAM.
 *     The maximum byte count is specified in the header file.
 *
 */
CyU3PReturnStatus_t
CyFxBulkLpFramRead (
    uint16_t    sector,
    uint8_t     *buffer,
    uint16_t    byteCount
) {
    CyU3PDmaBuffer_t inBuf_p;
    uint8_t location[4];
    uint32_t byteAddress = 0;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /*
     * Do nothing if 0 Byte data is required.
     */
    if (byteCount == 0) {
        return CY_U3P_SUCCESS;
    }

    /*
     * Calculate the address of the sector on the SPI FRAM
     * The address is calculated by the sector size specified
     * in the header file.
     */
    byteAddress  = CY_FX_SECTOR_SIZE * sector;
    CyU3PDebugPrint (2, "SPI FRAM read - addr: 0x%x, size: 0x%x.\r\n",
            byteAddress, byteCount);

    /*
     * Prepare READ command for SPI FRAM
     * A command code and three byte address are provided as preamble.
     */
    location[0] = 0x03; /* Read command. */
    location[1] = (byteAddress >> 16) & 0xFF;       /* MS byte */
    location[2] = (byteAddress >> 8) & 0xFF;
    location[3] = byteAddress & 0xFF;               /* LS byte */

    /*
     * Prepare DMA buffer descriptor for the SPI FRAM
     * Both size and count field have the size of the DMA buffer.
     */
    inBuf_p.buffer = buffer;
    inBuf_p.status = 0;
    inBuf_p.size   = CY_FX_BULKLP_DMA_BUF_SIZE;
    inBuf_p.count  = CY_FX_BULKLP_DMA_BUF_SIZE;

    /*
     * Assert Slave Select output
     * A SPI transfer begins.
     */
    CyU3PSpiSetSsnLine (CyFalse);

    /*
     * Send a READ command to the SPI FRAM
     */
    status = CyU3PSpiTransmitWords (location, 4);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (2, "SPI READ command failed\r\n");
        CyU3PSpiSetSsnLine (CyTrue);
        return status;
    }

    /*
     * Prepare SPI transfer for READ
     * This API specifies how many words will be read from the SPI.
     * In the other words, this specifies the number of SCK pulses
     * to be generated.
     */
    CyU3PSpiSetBlockXfer (0, byteCount);

    /*
     * Connect the DMA buffer to DMA Channel
     * The DMA buffer descriptor is provided to the DMA Channel
     * to read and store the data from the SPI FRAM.
     */
    status = CyU3PDmaChannelSetupRecvBuffer (&glSpiRxHandle,  &inBuf_p);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PSpiSetSsnLine (CyTrue);
        return status;
    }

    /*
     * Wait for the SPI block transfer completed
     * This API returns when the SCK pulses are generated
     * to get the data from the SPI FRAM.
     */
    status = CyU3PSpiWaitForBlockXfer(CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PSpiSetSsnLine (CyTrue);
        return status;
    }

    /*
     * Finalize the DMA channel to disconnect from SPI
     * This API finished the DMA channel transfer whenever the DMA
     * buffer is not full.
     */
    status = CyU3PDmaChannelSetWrapUp(&glSpiRxHandle);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PSpiSetSsnLine (CyTrue);
        return status;
    }

    /*
     * Negate Slave Select output
     * This indicates the end of SPI transfer.
     */
    CyU3PSpiSetSsnLine (CyTrue);

    /*
     * Stop Block Transfer of SPI
     * Disable the block transfer mode of SPI to disconnect
     * from the DMA channel.
     */
    CyU3PSpiDisableBlockXfer (CyFalse, CyTrue);

    return CY_U3P_SUCCESS;
}

/*
 * Write a data packet to the specified sector
 *
 * Parameters
 *
 * uint16_t sector
 *     The sector number where the data to be written is located.
 *     The maximum number is specified in the header file.
 *     No sector number validation is implemented in this function.
 * uint8_t *buffer
 *     Buffer address where the data to be written is stored.
 *     The buffer should be a 32 byte aligned address because the
 *     value is directly used for the DMA channel buffer.
 * uint16_t byteCount
 *     The number of bytes to be written to the SPI FRAM.
 *     The maximum byte count is specified in the header file.
 *
 */
CyU3PReturnStatus_t
CyFxBulkLpFramWrite (
    uint16_t    sector,
    uint8_t     *buffer,
    uint16_t    byteCount
) {
    CyU3PDmaBuffer_t outBuf_p;
    uint8_t wren[1] = {0x06};  // WREN command
    uint8_t location[4];
    uint32_t byteAddress = 0;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /*
     * Do nothing if 0 Byte data is required
     */
    if (byteCount == 0) {
        return CY_U3P_SUCCESS;
    }

    /*
     * Calculate the address of the sector on the SPI FRAM
     * The address is calculated by the sector size specified
     * in the header file.
     */
    byteAddress  = CY_FX_SECTOR_SIZE * sector;
    CyU3PDebugPrint (2, "SPI FRAM write - addr: 0x%x, size: 0x%x.\r\n",
            byteAddress, byteCount);

    /*
     * Prepare WRITE command for SPI FRAM
     * A command code and three byte address are provided as preamble.
     */
    location[0] = 0x02; /* Write command */
    location[1] = (byteAddress >> 16) & 0xFF;       /* MS byte */
    location[2] = (byteAddress >> 8) & 0xFF;
    location[3] = byteAddress & 0xFF;               /* LS byte */

    /*
     * Prepare DMA buffer descriptor for SPI FRAM
     * Prepare DMA buffer descriptor for the SPI FRAM
     * The size field have the size of the DMA buffer.
     * The count field has the byte count to be written.
     */
    outBuf_p.buffer = buffer;
    outBuf_p.status = 0;
    outBuf_p.size   = CY_FX_BULKLP_DMA_BUF_SIZE;
    outBuf_p.count  = byteCount;

    /*
     * Send WREN command to enable WRITE operations
     * WREN command should be issued prior the WRITE command.
     */
    CyU3PSpiSetSsnLine (CyFalse);
    status = CyU3PSpiTransmitWords (wren, 1);
    CyU3PSpiSetSsnLine (CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (2, "FRAM WREN command failed\n\r");
        return status;
    }

    /*
     * Assert Slave Select output
     * A SPI transfer begins.
     */
    CyU3PSpiSetSsnLine (CyFalse);

    /*
     * Send a WRITE command to the SPI FRAM
     */
    status = CyU3PSpiTransmitWords (location, 4);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (2, "SPI WRITE command failed\r\n");
        CyU3PSpiSetSsnLine (CyTrue);
        return status;
    }

    /*
     * Prepare SPI transfer for WRITE
     * This API specifies how many words will be written to the SPI.
     * In the other words, this specifies the number of SCK pulses
     * to be generated.
     */
    CyU3PSpiSetBlockXfer (byteCount, 0);

    /*
     * Connect the DMA buffer to DMA Channel
     * The DMA buffer descriptor is provided to the DMA Channel
     * to write the data to the SPI FRAM.
     */
    status = CyU3PDmaChannelSetupSendBuffer (&glSpiTxHandle, &outBuf_p);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PSpiSetSsnLine (CyTrue);
        return status;
    }

    /*
     * Wait for the DMA Channel completed
     * This API returns when all data stored in the DMA buffer are sent.
     */
    status = CyU3PDmaChannelWaitForCompletion(&glSpiTxHandle,
            CY_FX_FRAM_TIMEOUT);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PSpiSetSsnLine (CyTrue);
        return status;
    }

    /*
     * Negate Slave Select output
     * This indicates the end of SPI transfer.
     */
    CyU3PSpiSetSsnLine (CyTrue);

    /*
     * Stop Block Transfer of SPI
     * Disable the block transfer mode of SPI to disconnect
     * from the DMA channel.
     */
    CyU3PSpiDisableBlockXfer (CyTrue, CyFalse);

    return CY_U3P_SUCCESS;
}

/* This function starts the bulk loop application. This is called
 * when a SET_CONF event is received from the USB host. The endpoints
 * are configured and the DMA pipe is setup in this function. */
void
CyFxBulkLpApplnStart (
        void)
{
    uint16_t size = 0;
    CyU3PEpConfig_t epCfg;
    CyU3PDmaChannelConfig_t dmaCfg;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    CyU3PUSBSpeed_t usbSpeed = CyU3PUsbGetSpeed();

    /* First identify the usb speed. Once that is identified,
     * create a DMA channel and start the transfer on this. */

    /* Based on the Bus Speed configure the endpoint packet size */
    switch (usbSpeed)
    {
        case CY_U3P_FULL_SPEED:
            size = 64;
            break;

        case CY_U3P_HIGH_SPEED:
            size = 512;
            break;

        case  CY_U3P_SUPER_SPEED:
            size = 1024;
            break;

        default:
            CyU3PDebugPrint (4, "Error! Invalid USB speed.\n");
            CyFxAppErrorHandler (CY_U3P_ERROR_FAILURE);
            break;
    }

    CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
    epCfg.enable = CyTrue;
    epCfg.epType = CY_U3P_USB_EP_BULK;
    epCfg.burstLen = 1;
    epCfg.streams = 0;
    epCfg.pcktSize = size;
    glPacketSize = size;

    /* Producer endpoint configuration */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_PRODUCER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Consumer endpoint configuration */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Flush the endpoint memory */
    CyU3PUsbFlushEp(CY_FX_EP_PRODUCER);
    CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);

    /* Create a DMA MANUAL_IN channel for the producer socket. */
    // The DMA channel buffer size is independent to the USB bus speed.
    dmaCfg.size  = CY_FX_BULKLP_DMA_BUF_SIZE;
    dmaCfg.count = CY_FX_BULKLP_DMA_BUF_COUNT;
    dmaCfg.prodSckId = CY_FX_EP_PRODUCER_SOCKET;
    dmaCfg.consSckId = CY_U3P_CPU_SOCKET_CONS;
    dmaCfg.dmaMode = CY_U3P_DMA_MODE_BYTE;
    /* No callback is required. */
    dmaCfg.notification = 0;
    dmaCfg.cb = NULL;
    dmaCfg.prodHeader = 0;
    dmaCfg.prodFooter = 0;
    dmaCfg.consHeader = 0;
    dmaCfg.prodAvailCount = 0;

    apiRetStatus = CyU3PDmaChannelCreate (&glChHandleBulkLpIn,
            CY_U3P_DMA_TYPE_MANUAL_IN, &dmaCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelCreate failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Create a DMA MANUAL_OUT channel for the consumer socket. */
    dmaCfg.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    dmaCfg.consSckId = CY_FX_EP_CONSUMER_SOCKET;
    apiRetStatus = CyU3PDmaChannelCreate (&glChHandleBulkLpOut,
            CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelCreate failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set DMA Channel transfer size */
    apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleBulkLpIn, CY_FX_BULKLP_DMA_TX_SIZE);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer Failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleBulkLpOut, CY_FX_BULKLP_DMA_TX_SIZE);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer Failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Update the flag so that the application thread is notified of this. */
    glIsApplnActive = CyTrue;
}

/* This function stops the bulk loop application. This shall be called whenever
 * a RESET or DISCONNECT event is received from the USB host. The endpoints are
 * disabled and the DMA pipe is destroyed by this function. */
void
CyFxBulkLpApplnStop (
        void)
{
    CyU3PEpConfig_t epCfg;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Update the flag so that the application thread is notified of this. */
    glIsApplnActive = CyFalse;

    /* Destroy the channels */
    CyU3PDmaChannelDestroy (&glChHandleBulkLpIn);
    CyU3PDmaChannelDestroy (&glChHandleBulkLpOut);

    /* Flush the endpoint memory */
    CyU3PUsbFlushEp(CY_FX_EP_PRODUCER);
    CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);

    /* Disable endpoints. */
    CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
    epCfg.enable = CyFalse;

    /* Producer endpoint configuration. */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_PRODUCER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Consumer endpoint configuration. */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }
}

/* Callback to handle the USB setup requests. */
CyBool_t
CyFxBulkLpApplnUSBSetupCB (
        uint32_t setupdat0, /* SETUP Data 0 */
        uint32_t setupdat1  /* SETUP Data 1 */
    )
{
    /* Fast enumeration is used. Only requests addressed to the interface, class,
     * vendor and unknown control requests are received by this function.
     * This application does not support any class or vendor requests. */

    uint8_t  bRequest, bReqType;
    uint8_t  bType, bTarget;
    uint16_t wValue, wIndex;
    CyBool_t isHandled = CyFalse;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /* Decode the fields from the setup request. */
    bReqType = (setupdat0 & CY_U3P_USB_REQUEST_TYPE_MASK);
    bType    = (bReqType & CY_U3P_USB_TYPE_MASK);
    bTarget  = (bReqType & CY_U3P_USB_TARGET_MASK);
    bRequest = ((setupdat0 & CY_U3P_USB_REQUEST_MASK) >> CY_U3P_USB_REQUEST_POS);
    wValue   = ((setupdat0 & CY_U3P_USB_VALUE_MASK)   >> CY_U3P_USB_VALUE_POS);
    wIndex   = ((setupdat1 & CY_U3P_USB_INDEX_MASK)   >> CY_U3P_USB_INDEX_POS);

    if (bType == CY_U3P_USB_STANDARD_RQT)
    {
        /* Handle SET_FEATURE(FUNCTION_SUSPEND) and CLEAR_FEATURE(FUNCTION_SUSPEND)
         * requests here. It should be allowed to pass if the device is in configured
         * state and failed otherwise. */
        if ((bTarget == CY_U3P_USB_TARGET_INTF) && ((bRequest == CY_U3P_USB_SC_SET_FEATURE)
                    || (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE)) && (wValue == 0))
        {
            if (glIsApplnActive)
                CyU3PUsbAckSetup ();
            else
                CyU3PUsbStall (0, CyTrue, CyFalse);

            isHandled = CyTrue;
        }

        /* CLEAR_FEATURE request for endpoint is always passed to the setup callback
         * regardless of the enumeration model used. When a clear feature is received,
         * the previous transfer has to be flushed and cleaned up. This is done at the
         * protocol level. Since this is just a loopback operation, there is no higher
         * level protocol and there are two DMA channels associated with the function,
         * it is easier to stop and restart the application. If there are more than one
         * EP associated with the channel reset both the EPs. The endpoint stall and toggle
         * / sequence number is also expected to be reset. Return CyFalse to make the
         * library clear the stall and reset the endpoint toggle. Or invoke the
         * CyU3PUsbStall (ep, CyFalse, CyTrue) and return CyTrue. Here we are clearing
         * the stall. */
        if ((bTarget == CY_U3P_USB_TARGET_ENDPT) && (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE)
                && (wValue == CY_U3P_USBX_FS_EP_HALT))
        {
            if ((wIndex == CY_FX_EP_PRODUCER) || (wIndex == CY_FX_EP_CONSUMER))
            {
                if (glIsApplnActive)
                {
                    CyFxBulkLpApplnStop ();
                    /* Give a chance for the main thread loop to run. */
                    CyU3PThreadSleep (1);
                    CyFxBulkLpApplnStart ();
                    CyU3PUsbStall (wIndex, CyFalse, CyTrue);

                    CyU3PUsbAckSetup ();
                    isHandled = CyTrue;
                }
            }
        }
    }

    /* Handle supported vendor requests. */
    if (bType == CY_U3P_USB_VENDOR_RQT) {
        switch (bRequest) {
            case CY_FX_RQT_FRAM_WRITE:
                if (wIndex < CY_FX_N_SECTORS) {
                    glSectorToWrite = wIndex;
                    CyU3PEventSet (&glFramEvent, CY_FX_FRAM_WRITE_READY, CYU3P_EVENT_OR);
                    CyU3PUsbAckSetup();
                    isHandled = CyTrue;
                }
                break;
            case CY_FX_RQT_FRAM_READ:
                if (wIndex < CY_FX_N_SECTORS) {
                    glSectorToRead = wIndex;
                    glSizeToRead = wValue;
                    CyU3PEventSet (&glFramEvent, CY_FX_FRAM_READ_READY, CYU3P_EVENT_OR);
                    CyU3PUsbAckSetup();
                    isHandled = CyTrue;
                }
                break;
        }
    }

    /* If there was any error, return not handled so that the library will
     * stall the request. Alternatively EP0 can be stalled here and return
     * CyTrue. */
    if (status != CY_U3P_SUCCESS) {
        isHandled = CyFalse;
    }

    return isHandled;
}

/* This is the callback function to handle the USB events. */
void
CyFxBulkLpApplnUSBEventCB (
    CyU3PUsbEventType_t evtype, /* Event type */
    uint16_t            evdata  /* Event data */
    )
{
    switch (evtype)
    {
        case CY_U3P_USB_EVENT_SETCONF:
            /* Stop the application before re-starting. */
            if (glIsApplnActive)
            {
                CyFxBulkLpApplnStop ();
            }
            /* Start the loop back function. */
            CyFxBulkLpApplnStart ();
            break;

        case CY_U3P_USB_EVENT_RESET:
        case CY_U3P_USB_EVENT_DISCONNECT:
            /* Stop the loop back function. */
            if (glIsApplnActive)
            {
                CyFxBulkLpApplnStop ();
            }
            break;

        default:
            break;
    }
}

/* Callback function to handle LPM requests from the USB 3.0 host. This function is invoked by the API
   whenever a state change from U0 -> U1 or U0 -> U2 happens. If we return CyTrue from this function, the
   FX3 device is retained in the low power state. If we return CyFalse, the FX3 device immediately tries
   to trigger an exit back to U0.

   This application does not have any state in which we should not allow U1/U2 transitions; and therefore
   the function always return CyTrue.
 */
CyBool_t
CyFxBulkLpApplnLPMRqtCB (
        CyU3PUsbLinkPowerMode link_mode)
{
    return CyTrue;
}

/* This function initializes the USB Module, sets the enumeration descriptors.
 * This function does not start the bulk streaming and this is done only when
 * SET_CONF event is received. */
CyU3PReturnStatus_t
CyFxBulkLpApplnInit (void)
{
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /* Initialize the SPI interface for flash of page size 256 bytes. */
    status = CyFxBulkLpSpiInit ();
    if (status != CY_U3P_SUCCESS) {
        return status;
    }

    /* Start the USB functionality. */
    apiRetStatus = CyU3PUsbStart();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PUsbStart failed to Start, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* The fast enumeration is the easiest way to setup a USB connection,
     * where all enumeration phase is handled by the library. Only the
     * class / vendor requests need to be handled by the application. */
    CyU3PUsbRegisterSetupCallback(CyFxBulkLpApplnUSBSetupCB, CyTrue);

    /* Setup the callback to handle the USB events. */
    CyU3PUsbRegisterEventCallback(CyFxBulkLpApplnUSBEventCB);

    /* Register a callback to handle LPM requests from the USB 3.0 host. */
    CyU3PUsbRegisterLPMRequestCallback(CyFxBulkLpApplnLPMRqtCB);    
    
    /* Set the USB Enumeration descriptors */

    /* Super speed device descriptor. */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_DEVICE_DESCR, NULL, (uint8_t *)CyFxUSB30DeviceDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* High speed device descriptor. */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_DEVICE_DESCR, NULL, (uint8_t *)CyFxUSB20DeviceDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* BOS descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_BOS_DESCR, NULL, (uint8_t *)CyFxUSBBOSDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Device qualifier descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_DEVQUAL_DESCR, NULL, (uint8_t *)CyFxUSBDeviceQualDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set device qualifier descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Super speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_CONFIG_DESCR, NULL, (uint8_t *)CyFxUSBSSConfigDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* High speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_CONFIG_DESCR, NULL, (uint8_t *)CyFxUSBHSConfigDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Set Other Speed Descriptor failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Full speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_FS_CONFIG_DESCR, NULL, (uint8_t *)CyFxUSBFSConfigDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Set Configuration Descriptor failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 0 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 1 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 2 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Connect the USB Pins with super speed operation enabled. */
    apiRetStatus = CyU3PConnectState(CyTrue, CyTrue);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Connect failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    return CY_U3P_SUCCESS;
}

/* Entry function for the BulkLpAppThread. */
void
BulkLpAppThread_Entry (
        uint32_t input)
{
    CyU3PDmaBuffer_t inBuf_p, outBuf_p;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
    uint32_t eventFlags;

    /* Initialize the debug module */
    CyFxBulkLpApplnDebugInit();

    /* Initialize the bulk loop application */
    status = CyFxBulkLpApplnInit();
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_error;
    }

    for (;;) {
        if (glIsApplnActive) {
            status = CyU3PEventGet(&glFramEvent,
                CY_FX_FRAM_WRITE_READY | CY_FX_FRAM_READ_READY,
                CYU3P_EVENT_OR_CLEAR,
                &eventFlags,
                CYU3P_NO_WAIT
            );
            if (status != CY_U3P_SUCCESS) {
                continue;
            }
            if (eventFlags & CY_FX_FRAM_WRITE_READY) {
                /*
                 * Wait for receiving a buffer from the producer socket (OUT endpoint). The call
                 * will fail if there was an error or if the USB connection was reset / disconnected.
                 * In case of error invoke the error handler and in case of reset / disconnection,
                 * glIsApplnActive will be CyFalse; continue to beginning of the loop.
                 */
                status = CyU3PDmaChannelGetBuffer (&glChHandleBulkLpIn, &inBuf_p, CYU3P_WAIT_FOREVER);
                if (status != CY_U3P_SUCCESS) {
                    if (!glIsApplnActive) {
                        continue;
                    } else {
                        CyU3PDebugPrint (4, "CyU3PDmaChannelGetBuffer failed, Error code = %d\n", status);
                        CyFxAppErrorHandler(status);
                    }
                }

                /*
                 * Write a data packet to FRAM at a sector previously
                 * specified by the FRAM_WRITE control request.
                 */
                status = CyFxBulkLpFramWrite(glSectorToWrite, inBuf_p.buffer, inBuf_p.count);
                if (status != CY_U3P_SUCCESS) {
                    if (!glIsApplnActive) {
                        continue;
                    } else {
                        CyU3PDebugPrint (4, "CyFxBulkLpFramWrite failed, Error code = %d\n", status);
                        CyFxAppErrorHandler(status);
                    }
                }

                /*
                 * Now discard the data from the producer channel so that the buffer is made available
                 * to receive more data.
                 */
                status = CyU3PDmaChannelDiscardBuffer (&glChHandleBulkLpIn);
                if (status != CY_U3P_SUCCESS) {
                    if (!glIsApplnActive) {
                        continue;
                    } else {
                        CyU3PDebugPrint (4, "CyU3PDmaChannelDiscardBuffer failed, Error code = %d\n", status);
                        CyFxAppErrorHandler(status);
                    }
                }
            }
            if (eventFlags & CY_FX_FRAM_READ_READY) {
                /*
                 * Wait for a free buffer to be used to transmit the received data.
                 * The failure cases are same as above.
                 */
                status = CyU3PDmaChannelGetBuffer (&glChHandleBulkLpOut, &outBuf_p, CYU3P_WAIT_FOREVER);
                if (status != CY_U3P_SUCCESS) {
                    if (!glIsApplnActive) {
                        continue;
                    } else {
                        CyU3PDebugPrint (4, "CyU3PDmaChannelGetBuffer failed, Error code = %d\n", status);
                        CyFxAppErrorHandler(status);
                    }
                }

                /*
                 * Read a data packet from FRAM at a sector previously
                 * specified by the FRAM_READ control request.
                 */
                status = CyFxBulkLpFramRead(glSectorToRead, outBuf_p.buffer, glSizeToRead);
                if (status != CY_U3P_SUCCESS) {
                    if (!glIsApplnActive) {
                        continue;
                    } else {
                        CyU3PDebugPrint (4, "CyFxBulkLpFramRead failed, Error code = %d\n", status);
                        CyFxAppErrorHandler(status);
                    }
                }

                /*
                 * Commit the received data to the consumer pipe so that the data can be
                 * transmitted back to the USB host. Since the same data is sent back, the
                 * count shall be same as received and the status field of the call shall
                 * be 0 for default use case.
                 */
                status = CyU3PDmaChannelCommitBuffer (&glChHandleBulkLpOut, glSizeToRead, 0);
                if (status != CY_U3P_SUCCESS) {
                    if (!glIsApplnActive) {
                        continue;
                    } else {
                        CyU3PDebugPrint (4, "CyU3PDmaChannelCommitBuffer failed, Error code = %d\n", status);
                        CyFxAppErrorHandler(status);
                    }
                }
                if ((glSizeToRead > 0) && ((glSizeToRead % glPacketSize) == 0)) {
                    /*
                     * Add ZLP for aligned size of data
                     */
                    status = CyU3PDmaChannelGetBuffer (&glChHandleBulkLpOut, &outBuf_p, CYU3P_WAIT_FOREVER);
                    if (status != CY_U3P_SUCCESS) {
                        if (!glIsApplnActive) {
                            continue;
                        } else {
                            CyU3PDebugPrint (4, "CyU3PDmaChannelGetBuffer failed, Error code = %d\n", status);
                            CyFxAppErrorHandler(status);
                        }
                    }
                    status = CyU3PDmaChannelCommitBuffer (&glChHandleBulkLpOut, 0, 0);
                    if (status != CY_U3P_SUCCESS) {
                        if (!glIsApplnActive) {
                            continue;
                        } else {
                            CyU3PDebugPrint (4, "CyU3PDmaChannelCommitBuffer failed, Error code = %d\n", status);
                            CyFxAppErrorHandler(status);
                        }
                    }
                }
            }
        } else {
            /* No active data transfer. Sleep for a small amount of time. */
            CyU3PThreadSleep (100);
        }
    }

handle_error:
    CyU3PDebugPrint (4, "Application failed to initialize. Error code: %d.\n", status);
    while (1);
}

/* Application define function which creates the threads. */
void
CyFxApplicationDefine (
        void)
{
    void *ptr = NULL;
    uint32_t status = CY_U3P_SUCCESS;
    uint32_t retThrdCreate = CY_U3P_SUCCESS;

    status = CyU3PEventCreate (&glFramEvent);
    if (status != 0) {
        /* Loop indefinitely */
        while (1);
    }

    /* Allocate the memory for the threads */
    ptr = CyU3PMemAlloc (CY_FX_BULKLP_THREAD_STACK);

    /* Create the thread for the application */
    retThrdCreate = CyU3PThreadCreate (&BulkLpAppThread,           /* Bulk loop App Thread structure */
                          "21:Bulk_loop_MANUAL_IN_OUT",            /* Thread ID and Thread name */
                          BulkLpAppThread_Entry,                   /* Bulk loop App Thread Entry function */
                          0,                                       /* No input parameter to thread */
                          ptr,                                     /* Pointer to the allocated thread stack */
                          CY_FX_BULKLP_THREAD_STACK,               /* Bulk loop App Thread stack size */
                          CY_FX_BULKLP_THREAD_PRIORITY,            /* Bulk loop App Thread priority */
                          CY_FX_BULKLP_THREAD_PRIORITY,            /* Bulk loop App Thread priority */
                          CYU3P_NO_TIME_SLICE,                     /* No time slice for the application thread */
                          CYU3P_AUTO_START                         /* Start the Thread immediately */
                          );

    /* Check the return code */
    if (retThrdCreate != 0)
    {
        /* Thread Creation failed with the error code retThrdCreate */

        /* Add custom recovery or debug actions here */

        /* Application cannot continue */
        /* Loop indefinitely */
        while(1);
    }
}

/*
 * Main function
 */
int
main (void)
{
    CyU3PIoMatrixConfig_t io_cfg;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /* Initialize the device */
    status = CyU3PDeviceInit (NULL);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Initialize the caches. Enable both Instruction and Data Caches. */
    status = CyU3PDeviceCacheControl (CyTrue, CyTrue, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Configure the IO matrix for the device. On the FX3 DVK board, the COM port 
     * is connected to the IO(53:56). This means that either DQ32 mode should be
     * selected or lppMode should be set to UART_ONLY. Here we are choosing
     * UART_ONLY configuration. */
    io_cfg.isDQ32Bit = CyFalse;
    io_cfg.s0Mode = CY_U3P_SPORT_INACTIVE;
    io_cfg.s1Mode = CY_U3P_SPORT_INACTIVE;
    io_cfg.useUart   = CyTrue;
    io_cfg.useI2C    = CyFalse;
    io_cfg.useI2S    = CyFalse;
    io_cfg.useSpi    = CyTrue;
    io_cfg.lppMode   = CY_U3P_IO_MATRIX_LPP_DEFAULT;
    /* No GPIOs are enabled. */
    io_cfg.gpioSimpleEn[0]  = 0;
    io_cfg.gpioSimpleEn[1]  = 0;
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    status = CyU3PDeviceConfigureIOMatrix (&io_cfg);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* This is a non returnable call for initializing the RTOS kernel */
    CyU3PKernelEntry ();

    /* Dummy return to make the compiler happy */
    return 0;

handle_fatal_error:

    /* Cannot recover from this error. */
    while (1);
}

/* [ ] */

