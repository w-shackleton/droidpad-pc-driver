/*++

 * This file is part of DroidPad.
 * DroidPad lets you use an Android mobile to control a joystick or mouse
 * on a Windows or Linux computer.
 * This program is the driver for DroidPad's Joystick.
 *
 * DroidPad is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DroidPad is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with DroidPad, in the file COPYING.
 * If not, see <http://www.gnu.org/licenses/>.

Module Name:

    droidpad.h
    
Abstract:

    common header file

Author:


Environment:

    kernel mode only

Notes:


Revision History:


--*/
#ifndef _DROIDPAD_DRIVER_H_

#define _DROIDPAD_DRIVER_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>
#include "usbdi.h"
#include "usbdlib.h"

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>
#include "wdfusb.h"

#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <hidport.h>

#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

#include "trace.h"

#include "../inc/defs.h"

#define _DRIVER_NAME_                 "DroidPad: "
#define COMPATIBLE_DEVICE_ID		  L"hid_device_system_game"

#define READ_REPORT_MILLIS			50

WDFCOLLECTION deviceCollection;
WDFWAITLOCK deviceCollectionLock;
extern WDFDEVICE controlDevice;

static int deviceCounter;
WDFWAITLOCK deviceCounterLock;

typedef struct _CONTROL_DEVICE_EXTENSION {

    PVOID   ControlData;
    WDFDEVICE hParentDevice;

} CONTROL_DEVICE_EXTENSION, *PCONTROL_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CONTROL_DEVICE_EXTENSION, ControlGetData)

typedef UCHAR HID_REPORT_DESCRIPTOR, *PHID_REPORT_DESCRIPTOR;

// HID descriptor of a 6-axis 12-button JS, which DroidPad uses.

// Halfway on each axis
#define JS_RESTING_PLACE 16384

#ifdef USE_HARDCODED_HID_REPORT_DESCRIPTOR 

CONST  HID_REPORT_DESCRIPTOR       G_DefaultReportDescriptor[79] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x15, 0x00,                    // LOGICAL_MINIMUM (0)
    0x09, 0x04,                    // USAGE (Joystick)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x05, 0x01,                    //   USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    //   USAGE (Pointer)
    0x15, 0x00, 	               //   LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f,              //   LOGICAL_MAXIMUM (32767)
    0x75, 0x20,                    //   REPORT_SIZE (32)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x09, 0x30,                    //     USAGE (X)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs) -- CHANGED TO SEQUENTIAL
    0x09, 0x31,                    //     USAGE (Y)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x09, 0x32,                    //     USAGE (Rx)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x09, 0x33,                    //     USAGE (Ry)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x09, 0x34,                    //     USAGE (Slider)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x09, 0x35,                    //     USAGE (Dial)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x81, 0x01,                    //     INPUT (Cnst,Ary,Abs)
    0x81, 0x01,                    //     INPUT (Cnst,Ary,Abs)
    0xc0,                          //   END_COLLECTION
    0x05, 0x09,                    //   USAGE_PAGE (Button)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x55, 0x00,                    //   UNIT_EXPONENT (0)
    0x65, 0x00,                    //   UNIT (None)
    0x19, 0x01,                    //   USAGE_MINIMUM (Button 1)
    0x29, 0x0c,                    //   USAGE_MAXIMUM (Button 12)
    0x95, 0x0c,                    //   REPORT_COUNT (12)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x75, 0x14,                    //   REPORT_SIZE (20)
    0x81, 0x01,                    //   INPUT (Cnst,Ary,Abs)
    0xc0                           // END_COLLECTION
};

//
// This is the default HID descriptor returned by the mini driver
// in response to IOCTL_HID_GET_DEVICE_DESCRIPTOR. The size
// of report descriptor is currently the size of G_DefaultReportDescriptor.
//
CONST HID_DESCRIPTOR G_DefaultHidDescriptor = {
    0x09,   // length of HID descriptor
    0x21,   // descriptor type == HID  0x21
    0x0100, // hid spec release
    0x00,   // country code == Not Specified
    0x01,   // number of HID class descriptors
    { 0x22,   // descriptor type 
    sizeof(G_DefaultReportDescriptor) }  // total length of report descriptor
};

#endif  // USE_HARDCODED_HID_REPORT_DESCRIPTOR

#include <pshpack1.h>
typedef struct _HID_INPUT_REPORT {
	union {
		struct {
			LONG	axisX;
			LONG	axisY;
			LONG	axisZ;
			LONG	axisRX;
			LONG	axisRY;
			LONG	axisRZ;
			LONG	_u1; // Unused
			LONG	_u2;
			USHORT	buttons;	// 16 Buttons (12 used)
			USHORT	_unused1;
		} inputs;
		UCHAR raw[36];
	};
} HID_INPUT_REPORT, *PHID_INPUT_REPORT;
#include <poppack.h>


typedef struct _DEVICE_EXTENSION{

    //
    // This variable stores state for the swicth that got toggled most recently
    // (the device returns the state of all the switches and not just the 
    // one that got toggled).
    // TODO: Delete this?
    UCHAR    LatestToggledSwitch;

    //
    // WDF Queue for timed IOCTL responses
    //
    WDFQUEUE   TimerMsgQueue;

    // HID report, which will be already filled in. Values must be copied from one to the other.
    HID_INPUT_REPORT inputs;

} DEVICE_EXTENSION, * PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, GetDeviceContext)

//
// driver routine declarations
//
// This type of function declaration is for Prefast for drivers. 
// Because this declaration specifies the function type, PREfast for Drivers
// does not need to infer the type or to report an inference. The declaration
// also prevents PREfast for Drivers from misinterpreting the function type 
// and applying inappropriate rules to the function. For example, PREfast for
// Drivers would not apply rules for completion routines to functions of type
// DRIVER_CANCEL. The preferred way to avoid Warning 28101 is to declare the
// function type explicitly. In the following example, the DriverEntry function
// is declared to be of type DRIVER_INITIALIZE.
//
DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD dpEvtDeviceAdd;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL dpEvtInternalDeviceControl;

NTSTATUS
dpGetHidDescriptor(
    IN WDFDEVICE Device,
    IN WDFREQUEST Request
    );

NTSTATUS
dpGetReportDescriptor(
    IN WDFDEVICE Device,
    IN WDFREQUEST Request
    );


NTSTATUS
dpGetDeviceAttributes(
    IN WDFREQUEST Request
    );

NTSTATUS
dpConfigContReaderForInterruptEndPoint(
    PDEVICE_EXTENSION DeviceContext
    );

EVT_WDF_USB_READER_COMPLETION_ROUTINE dpEvtUsbInterruptPipeReadComplete;

VOID
dpCompleteReadReport(
    WDFDEVICE Device
    );

EVT_WDF_OBJECT_CONTEXT_CLEANUP dpEvtDriverContextCleanup;

EVT_WDF_TIMER dpEvtTimerFunction;

VOID copyHidReport(
    IN PHID_INPUT_REPORT from,
    OUT PHID_INPUT_REPORT to);

NTSTATUS
dpCreateControlDevice(
    WDFDEVICE Device
    );
VOID
dpDeleteControlDevice(
    WDFDEVICE Device
    );

EVT_WDF_OBJECT_CONTEXT_CLEANUP dpEvtDeviceContextCleanup;

int deviceCounterChange(int difference);
#define deviceCounterIncrement() deviceCounterChange(1)
#define deviceCounterDecrement() deviceCounterChange(-1)
int deviceCounterReset();
/**
 * Gets the device count from the registry
 */
ULONG loadDeviceCount(PWSTR RegistryPath);

/**
 * gets the current device count
 */
int getDeviceCount();

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL dpEvtIoDeviceControl;

PCHAR
DbgHidInternalIoctlString(
    IN ULONG        IoControlCode
    );

VOID
copyInputData(
    IN PINPUT_DATA from,
    OUT PHID_INPUT_REPORT to
     );
VOID
resetHidReport(
				OUT PHID_INPUT_REPORT report
			  );

#if (OSVER(NTDDI_VERSION) > NTDDI_WIN2K)

NTSTATUS
dpSendIdleNotification(
    IN WDFREQUEST Request
    );

#endif  //(OSVER(NTDDI_VERSION) > NTDDI_WIN2K)

NTSTATUS
dpSetFeature(
    IN WDFREQUEST Request
    );

EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE dpEvtIoCanceledOnQueue;

NTSTATUS
SendVendorCommand(
    IN WDFDEVICE Device,
    IN UCHAR VendorCommand,
    IN PUCHAR CommandData
    );

#endif   //_DROIDPAD_DRIVER_H_


