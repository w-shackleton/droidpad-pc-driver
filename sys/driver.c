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

    driver.c

Abstract:

    Code for main entry point of KMDF driver

Author:


Environment:

    kernel mode only

Revision History:

--*/

#include <droidpad.h>

#if defined(EVENT_TRACING)
//
// The trace message header (.tmh) file must be included in a source file
// before any WPP macro calls and after defining a WPP_CONTROL_GUIDS
// macro (defined in toaster.h). During the compilation, WPP scans the source
// files for DoTraceMessage() calls and builds a .tmh file which stores a unique
// data GUID for each message, the text resource string for each message,
// and the data types of the variables passed in for each message.  This file
// is automatically generated and used during post-processing.
//
#include "driver.tmh"
#else
ULONG DebugLevel = TRACE_LEVEL_INFORMATION;
ULONG DebugFlag = 0xff;
#endif

#ifdef ALLOC_PRAGMA
    #pragma alloc_text( INIT, DriverEntry )
    #pragma alloc_text( PAGE, dpEvtDeviceAdd)
    #pragma alloc_text( PAGE, dpEvtDriverContextCleanup)
#endif

NTSTATUS
DriverEntry (
    __in PDRIVER_OBJECT  DriverObject,
    __in PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    Installable driver initialization entry point.
    This entry point is called directly by the I/O system.

Arguments:

    DriverObject - pointer to the driver object

    RegistryPath - pointer to a unicode string representing the path,
                   to driver-specific key in the registry.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    NTSTATUS               status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG      config;
    WDF_OBJECT_ATTRIBUTES  attributes;

    //
    // Initialize WPP Tracing
    //
    WPP_INIT_TRACING( DriverObject, RegistryPath );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
        "DroidPad Driver Built %s %s\n", __DATE__, __TIME__);

    WDF_DRIVER_CONFIG_INIT(&config, dpEvtDeviceAdd);

    // Since there is only one control-device for all the instances
    // of the physical device, we need an ability to get to particular instance
    // of the device in our FilterEvtIoDeviceControlForControl. For that we
    // will create a collection object and store filter device objects.        
    // The collection object has the driver object as a default parent.
    //
    status = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &deviceCollection);
    if (!NT_SUCCESS(status))
    {
        KdPrint( ("WdfCollectionCreate failed with status 0x%x\n", status));
        return status;
    }

    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = dpEvtDriverContextCleanup;

    //
    // Create a framework driver object to represent our driver.
    //
    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,      // Driver Attributes
                             &config,          // Driver Config Info
                             WDF_NO_HANDLE
                             );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfDriverCreate failed with status 0x%x\n", status);
        
        WPP_CLEANUP(DriverObject);
    }

    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &deviceCollectionLock);
    if (!NT_SUCCESS(status))
    {
		TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfWaitLockCreate(deviceCollectionLock) failed with status 0x%x\n", status);
        return status;
    }

    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &deviceCounterLock);
    if (!NT_SUCCESS(status))
    {
		TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfWaitLockCreate(deviceCounterLock) failed with status 0x%x\n", status);
        return status;
    }

	// Reset device counter to 0
	if (!deviceCounterReset())
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "device counter initialization failed\n");
		return STATUS_DRIVER_INTERNAL_ERROR;
	}

    return status;
}


NTSTATUS
dpEvtDeviceAdd(
    IN WDFDRIVER       Driver,
    IN PWDFDEVICE_INIT DeviceInit
    )
/*++
Routine Description:

    dpEvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. We create and initialize a WDF device object to
    represent a new instance of toaster device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS                      status = STATUS_SUCCESS;
    WDF_IO_QUEUE_CONFIG           queueConfig;
    WDF_OBJECT_ATTRIBUTES         attributes;
    WDFDEVICE                     hDevice;
    PDEVICE_EXTENSION             devContext = NULL;
    WDFQUEUE                      queue;
	DECLARE_CONST_UNICODE_STRING(CompatId, COMPATIBLE_DEVICE_ID);
    WDF_TIMER_CONFIG              timerConfig;
    WDFTIMER                      timerHandle;
	LONG						  serialNumber;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
        "dpEvtDeviceAdd called\n");

	serialNumber = deviceCounterIncrement();
	if (-1 > serialNumber)
	{
		TraceEvents(TRACE_LEVEL_WARNING, DBG_PNP, "DeviceCount Failed- vJoyEvtDeviceAdd aborting\n");
		return STATUS_UNSUCCESSFUL;
	}
	if (serialNumber>0)
	{
		TraceEvents(TRACE_LEVEL_WARNING, DBG_PNP, "DeviceCount returned Serial Number %d- vJoyEvtDeviceAdd aborting\n", serialNumber);
		deviceCounterDecrement();
		return STATUS_UNSUCCESSFUL;
	}

    //
    // Tell framework this is a filter driver. Filter drivers by default are  
    // not power policy owners. This works well for this driver because
    // HIDclass driver is the power policy owner for HID minidrivers.
    //
    WdfFdoInitSetFilter(DeviceInit);

	// Child device's compatible ID is "hid_device_system_game"
	// Additional ones may be added below
	WdfPdoInitAddCompatibleID(DeviceInit, &CompatId);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_EXTENSION);
	attributes.EvtCleanupCallback = dpEvtDeviceContextCleanup;

    //
    // Create a framework device object.This call will in turn create
    // a WDM device object, attach to the lower stack, and set the
    // appropriate flags and attributes.
    //
    status = WdfDeviceCreate(&DeviceInit, &attributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "WdfDeviceCreate failed with status code 0x%x\n", status);
        return status;
    }

    devContext = GetDeviceContext(hDevice);

	///////////  Add this device to the FilterDevice collection. /////////////
    // 
    //
    WdfWaitLockAcquire(deviceCollectionLock, NULL);
    //
    // WdfCollectionAdd takes a reference on the item object and removes
    // it when you call WdfCollectionRemove.
    //
    status = WdfCollectionAdd(deviceCollection, hDevice);
    if (!NT_SUCCESS(status)) 
	{
        KdPrint( ("WdfCollectionAdd failed with status code 0x%x\n", status));
		return status;
    }

    WdfWaitLockRelease(deviceCollectionLock);
	/////////////////////////////////////////////////////////////////////////


	/////////// Create a control device /////////////////////////////////////
    status = dpCreateControlDevice(hDevice);
    if (!NT_SUCCESS(status))
	{
        KdPrint( ("dpCreateControlDevice failed with status 0x%x\n", status));
		return status;
    }
	/////////////////////////////////////////////////////////////////////////
    
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoInternalDeviceControl = dpEvtInternalDeviceControl;

    status = WdfIoQueueCreate(hDevice,
                              &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &queue
                              );
    if (!NT_SUCCESS (status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "WdfIoQueueCreate failed 0x%x\n", status);
        return status;
    }

    //
    // Register a manual I/O queue for handling Interrupt Message Read Requests.
    // This queue will be used for storing Requests that need to wait for an
    // interrupt to occur before they can be completed.
    //
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

    //
    // This queue is used for requests that dont directly access the device. The
    // requests in this queue are serviced only when the device is in a fully
    // powered state and sends an interrupt. So we can use a non-power managed
    // queue to park the requests since we dont care whether the device is idle
    // or fully powered up.
    //
    queueConfig.PowerManaged = WdfFalse;

    status = WdfIoQueueCreate(hDevice,
                              &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &devContext->TimerMsgQueue
                              );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "WdfIoQueueCreate failed 0x%x\n", status);
        return status;
    }

    //	Create a timer that completes IOCTL_HID_READ_REPORT pending requests
	//	Calback function will be called by this timer every READ_REPORT_MILLIS
    WDF_TIMER_CONFIG_INIT(&timerConfig, dpEvtTimerFunction);
    timerConfig.AutomaticSerialization = FALSE;
	timerConfig.Period = READ_REPORT_MILLIS;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = hDevice;
    status = WdfTimerCreate( &timerConfig,&attributes,&timerHandle);
    if (!NT_SUCCESS(status)) 
	{
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfTimerCreate failed status:0x%x\n", status);
        return status;
    }
	WdfTimerStart(timerHandle, 100);
 	/////////////////////////////////////////////////////////////////////////////////////////

    // devContext->DebounceTimer = timerHandle;
    return status;
}


VOID
dpEvtDriverContextCleanup(
    IN WDFDRIVER Driver
    )
/*++
Routine Description:

    Free resources allocated in DriverEntry that are not automatically
    cleaned up framework.

Arguments:

    Driver - handle to a WDF Driver object.

Return Value:

    VOID.

--*/
{
    PAGED_CODE ();

    UNREFERENCED_PARAMETER(Driver);

    // TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Exit dpEvtDriverContextCleanup\n");

    WPP_CLEANUP( WdfDriverWdmGetDriverObject( Driver ));

}

/**
 * Timer call for IOCTL_HID_READ_REPORT
 */
VOID
dpEvtTimerFunction(
    IN WDFTIMER  Timer
    )
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_EXTENSION devContext = GetDeviceContext(WdfTimerGetParentObject(Timer));

	// Check for requests, then get if there is one
	WDFREQUEST request;
	if(NT_SUCCESS(WdfIoQueueRetrieveNextRequest(devContext->TimerMsgQueue, &request))) {
		size_t bytesReturned;
		PHID_INPUT_REPORT hidReport = NULL;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(HID_INPUT_REPORT), &hidReport, &bytesReturned);
        if (!NT_SUCCESS(status)) 
		{
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                "WdfRequestRetrieveOutputBuffer failed with status: 0x%x\n", status);
        } else {
			// Copy the input report values from the dev context to the buffer.
			copyHidReport(&devContext->inputs, hidReport);
		}

        WdfRequestCompleteWithInformation(request, status, bytesReturned);

    } else if (status != STATUS_NO_MORE_ENTRIES)
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,"WdfIoQueueRetrieveNextRequest status %08x\n", status);

    return;
}

/**
 * Copies the value of an hid report.
 */
VOID copyHidReport(
				IN PHID_INPUT_REPORT from,
				OUT PHID_INPUT_REPORT to)
{
		if(from == NULL || to == NULL) {
				TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "copyHidReport received a null argument\n");
				return;
		}
		to->inputs.axisX = from->inputs.axisX;
		to->inputs.axisY = from->inputs.axisY;
		to->inputs.axisZ = from->inputs.axisZ;
		to->inputs.axisRX = from->inputs.axisRX;
		to->inputs.axisRY = from->inputs.axisRY;
		to->inputs.axisRZ = from->inputs.axisRZ;
		to->inputs.buttons = from->inputs.buttons;
		return;
}

#if !defined(EVENT_TRACING)

VOID
TraceEvents    (
    IN ULONG   TraceEventsLevel,
    IN ULONG   TraceEventsFlag,
    IN PCCHAR  DebugMessage,
    ...
    )

/*++

Routine Description:

    Debug print for the sample driver.

Arguments:

    TraceEventsLevel - print level between 0 and 3, with 3 the most verbose

Return Value:

    None.

 --*/
 {
#if DBG
#define     TEMP_BUFFER_SIZE        512
    va_list    list;
    CHAR       debugMessageBuffer[TEMP_BUFFER_SIZE];
    NTSTATUS   status;

    va_start(list, DebugMessage);

    if (DebugMessage) {

        //
        // Using new safe string functions instead of _vsnprintf.
        // This function takes care of NULL terminating if the message
        // is longer than the buffer.
        //
        status = RtlStringCbVPrintfA( debugMessageBuffer,
                                      sizeof(debugMessageBuffer),
                                      DebugMessage,
                                      list );
        if(!NT_SUCCESS(status)) {

            DbgPrint (_DRIVER_NAME_": RtlStringCbVPrintfA failed 0x%x\n", status);
            return;
        }
        if (TraceEventsLevel <= TRACE_LEVEL_ERROR ||
            (TraceEventsLevel <= DebugLevel &&
             ((TraceEventsFlag & DebugFlag) == TraceEventsFlag))) {
            DbgPrint("%s%s", _DRIVER_NAME_, debugMessageBuffer);
        }
    }
    va_end(list);

    return;
#else
    UNREFERENCED_PARAMETER(TraceEventsLevel);
    UNREFERENCED_PARAMETER(TraceEventsFlag);
    UNREFERENCED_PARAMETER(DebugMessage);
#endif
}

#endif

