
                        CYPRESS SEMICONDUCTOR CORPORATION
                                    FX3 SDK

USB BULKLOOP-MANUAL_IN_OUT EXAMPLE
-------------------------------

  This is another variant of the USB bulk loopback application that shows
  the use of the ARM9 CPU loopback data to the USB host.
  
  Though the functionality implemented is the same as the other loopback
  examples, the implementation is different.  In this case, the data received
  on the 1-OUT endpoint is received by the firmware application, copied into
  another data buffer and then sent to the USB host through the 1-IN endpoint.

  Two separate DMA channels are used to implement the data path for the two
  endpoints, and there is no connectivity established at the hardware level
  between the two endpoints.

  Files:

    * cyfx_gcc_startup.S   : Start-up code for the ARM-9 core on the FX3 device.
      This assembly source file follows the syntax for the GNU assembler.

    * cyfxbulklpmaninout.h : Constant definitions for the bulk loop application.
      The USB connection speed, numbers and properties of the endpoints etc.
      can be selected through definitions in this file.

    * cyfxbulklpdscr.c     : C source file containing the USB descriptors that
      are used by this firmware example. VID and PID is defined in this file.

    * cyfxtx.c             : ThreadX RTOS wrappers and utility functions required
      by the FX3 API library.

    * cyfxbulklpmaninout.c : Main C source file that implements the bulk loopback
      example.

    * makefile             : GNU make compliant build script for compiling this
      example.

    Vendor Commands implemented:

    1.  Prepare WRITE to SPI FRAM
        bmRequestType = 0x40 (Out-Vendor-Device)
        bRequest      = 0xC2
        wValue        = N/A
        wIndex        = SPI FRAM sector number. The sector size is defined as 20512Bytes.
        wLength       = 0

        A BULK-OUT transfer follows to send a data packet to be written.
        The maximum data size is 20480Bytes.

    2.  Prepare READ from SPI FRAM
        bmRequestType = 0x40 (Out-Vendor-Device)
        bRequest      = 0xC3
        wValue        = Length of data to be read. The maximum data size is 201480Bytes.
        wIndex        = SPI FRAM sector number. The sector size is defined as 20512Bytes.
        wLength       = 0

        A BULK-IN transfer follows to receive a data packet read from FRAM.

[]

