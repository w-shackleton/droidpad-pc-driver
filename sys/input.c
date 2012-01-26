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

    input.c

Abstract:

    Code for handling inputs via control device from userland.
    This file is the only entirely new file, not based off code from hidusbfx2

Author:


Environment:

    kernel mode only

Revision History:

--*/

#include <droidpad.h>

#if defined(EVENT_TRACING)
#include "input.tmh"
#endif

WDFDEVICE controlDevice;

NTSTATUS
dpCreateControlDevice(
    WDFDEVICE Device
    )
/*++

Routine Description:

    This routine is called to create a control device object so that application
    can talk to the filter driver directly instead of going through the entire
    device stack. This kind of control device object is useful if the filter
    driver is underneath another driver which prevents ioctls not known to it
    or if the driver's dispatch routine is owned by some other (port/class)
    driver and it doesn't allow any custom ioctls.

    NOTE: Since the control device is global to the driver and accessible to
    all instances of the device this filter is attached to, we create only once
    when the first instance of the device is started and delete it when the
    last instance gets removed.

Arguments:

    Device - Handle to a filter device object.

Return Value:

    WDF status code

--*/
{
	PCONTROL_DEVICE_EXTENSION	ConDevContext = NULL;
    PWDFDEVICE_INIT             pInit = NULL;
    WDF_OBJECT_ATTRIBUTES       controlAttributes;
    WDF_IO_QUEUE_CONFIG         ioQueueConfig;
    NTSTATUS                    status;
    WDFQUEUE                    queue;
	UNICODE_STRING				ntDeviceName, symbolicLinkName;
	ANSI_STRING					ntDeviceNameA, symbolicLinkNameA;

	DECLARE_CONST_UNICODE_STRING(SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R, L"D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)(A;;GRGW;;;WD)(A;;GR;;;RC)");

    PAGED_CODE();
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Entering dpCreateControlDevice\n");

    //
    // First find out whether any Control Device has been created. If the
    // collection has more than one device then we know somebody has already
    // created or in the process of creating the device.
    //
    WdfWaitLockAcquire(deviceCollectionLock, NULL);

    if(WdfCollectionGetCount(deviceCollection) != 1) {
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Device already exists, not recreating\n");
		WdfWaitLockRelease(deviceCollectionLock);
		return STATUS_SUCCESS; // No need to recreate
    }
    WdfWaitLockRelease(deviceCollectionLock);


	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Creating Control Device\n");

    //
    //
    // In order to create a control device, we first need to allocate a
    // WDFDEVICE_INIT structure and set all properties.
    //
	    

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "dpCreateControlDevice: Calling WdfControlDeviceInitAllocate\n");
    pInit = WdfControlDeviceInitAllocate( WdfDeviceGetDriver(Device), &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);

    if (pInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Error;
    }

    //
    // Set exclusive to false so that more than one app can talk to the
    // control device simultaneously.
    //
    WdfDeviceInitSetExclusive(pInit, FALSE);

	//
	// Assign a name to the Control Device
	// It has to be a UNICODE name hence the conversions
	//
	RtlInitAnsiString(&ntDeviceNameA, TEXT(NTDEVICE_NAME_STRING));
	status = RtlAnsiStringToUnicodeString(&ntDeviceName, &ntDeviceNameA, TRUE);
    if (!NT_SUCCESS(status)) 
        goto Error;
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "dpCreateControlDevice: Calling WdfDeviceInitAssignName\n");
    status = WdfDeviceInitAssignName(pInit, &ntDeviceName);
    if (!NT_SUCCESS(status)) 
        goto Error;
	RtlFreeUnicodeString(&ntDeviceName);


    //
    // Specify the size of device context & create the Control Device
    //
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "dpCreateControlDevice: Creating Control Device\n");
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&controlAttributes, CONTROL_DEVICE_EXTENSION);
    status = WdfDeviceCreate(&pInit, &controlAttributes, &controlDevice);
    if (!NT_SUCCESS(status))
        goto Error;

    //
    // Create a symbolic link for the control object so that usermode can open
    // the device.
    //
 	// It has to be a UNICODE name hence the conversions
	//
	RtlInitAnsiString(&symbolicLinkNameA, TEXT(SYMBOLIC_NAME_STRING));
	status = RtlAnsiStringToUnicodeString(&symbolicLinkName, &symbolicLinkNameA, TRUE);
    if (!NT_SUCCESS(status)) 
        goto Error;
	status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLinkName);
    if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "Failed to create symbolic link (Native)\n");
        goto Error;
	}
	RtlFreeUnicodeString(&symbolicLinkName);

    //
    // Configure the default queue associated with the control device object
    // to be Serial so that request passed to EvtIoDeviceControl are serialized.
    //

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchSequential);

    ioQueueConfig.EvtIoDeviceControl = dpEvtIoDeviceControl;


    //
    // Framework by default creates non-power managed queues for
    // filter drivers.
    //
    status = WdfIoQueueCreate(controlDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status))
        goto Error;


	ConDevContext = ControlGetData(controlDevice);
	ConDevContext->hParentDevice = Device;


    //
    // Control devices must notify WDF when they are done initializing.   I/O is
    // rejected until this call is made.
    //
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "dpCreateControlDevice: Calling WdfControlFinishInitializing\n");
    WdfControlFinishInitializing(controlDevice);

    return STATUS_SUCCESS;

Error:

    if (pInit != NULL) {
        WdfDeviceInitFree(pInit);
    }

    if (controlDevice != NULL) {
        //
        // Release the reference on the newly created object, since
        // we couldn't initialize it.
        //
        WdfObjectDelete(controlDevice);
        controlDevice = NULL;
    }

    return status;
}



VOID
dpDeleteControlDevice(
    WDFDEVICE Device
    )
/*++

Routine Description:

    This routine deletes the control by doing a simple dereference.

Arguments:

    Device - Handle to a framework filter device object.

Return Value:

    WDF status code

--*/
{
    UNREFERENCED_PARAMETER(Device);

    PAGED_CODE();

	if (!controlDevice)
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "No Control Device to delete\n");
		return;
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Control Device: Purging queue\n");
	WdfIoQueuePurge(WdfDeviceGetDefaultQueue(controlDevice), WDF_NO_EVENT_CALLBACK, WDF_NO_CONTEXT);


	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Control Device: Deleting\n");

    if (controlDevice) {
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Control Device: Deleting (Just before WdfObjectDelete)\n");
        WdfObjectDelete(controlDevice);
        //WdfObjectDelete(Device);
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Control Device: Deleting (Just after WdfObjectDelete)\n");
       controlDevice = NULL;
    }
}

VOID
dpEvtDeviceContextCleanup(
    IN WDFDEVICE Device
    )
/**
 * Cleans up device context on remove
 */
{
    ULONG   count;

    PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Entered FilterEvtDeviceContextCleanup\n");

	count = deviceCounterDecrement();
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Device Count before decrementing is %d\n", count);
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Device Count after decrementing is %d\n", getDeviceCount());

    WdfWaitLockAcquire(deviceCollectionLock, NULL);

    if(WdfCollectionGetCount(deviceCollection) == 1)
	{
		// Delete control device if this is last instance
		dpDeleteControlDevice(Device);
    }

    WdfCollectionRemove(deviceCollection, Device);

    WdfWaitLockRelease(deviceCollectionLock);
}

// Device counter modifiers - each one acquires and releases the lock.

// Returns the old value
int
deviceCounterChange(int difference)
{
	NTSTATUS status = WdfWaitLockAcquire(deviceCounterLock, NULL);
	int old;
	if(!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "deviceCounterChange failed with status code 0x%x\n", status);
        return -1;
	}
	old = deviceCounter;
	deviceCounter += difference;
	WdfWaitLockRelease(deviceCounterLock);
	return old;
}
// Returns 0 on failure (similar to boolean type)
int deviceCounterReset()
{
	NTSTATUS status = WdfWaitLockAcquire(deviceCounterLock, NULL);
	if(!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "deviceCounterReset failed with status code 0x%x\n", status);
        return 0;
	}
	deviceCounter = 0;
	WdfWaitLockRelease(deviceCounterLock);
	return 1;
}

/**
 * gets the current device count
 */
int getDeviceCount() {
	NTSTATUS status = WdfWaitLockAcquire(deviceCounterLock, NULL);
	int val;
	if(!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "getDeviceCount failed with status code 0x%x\n", status);
        return -1;
	}
	val = deviceCounter;
	WdfWaitLockRelease(deviceCounterLock);
	return val;
}

VOID
dpEvtIoDeviceControl(
    IN WDFQUEUE     Queue,
    IN WDFREQUEST   Request,
    IN size_t       OutputBufferLength,
    IN size_t       InputBufferLength,
    IN ULONG        IoControlCode
    )
/**
 * This is called when an IOCTL is received from the control device created above.
 * It is used to receive signals & messages from userland applications (namely DroidPad).
 */
{
    NTSTATUS             status= STATUS_SUCCESS;
    WDF_DEVICE_STATE     deviceState;
    WDFDEVICE            hDevice = WdfIoQueueGetDevice(Queue);
	PCONTROL_DEVICE_EXTENSION			 ControlDevContext = ControlGetData(hDevice);
	ULONG  bytes;
    PDEVICE_EXTENSION    pDevContext = NULL;
    PVOID  buffer;
    size_t  bufSize;
	PINPUT_DATA jsData;
	size_t	bytesReturned = 0;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	// KdPrint(("dpEvtIoDeviceControl called\n"));

	PAGED_CODE();

	switch (IoControlCode) {

	case IOCTL_DP_SEND_INPUT_DATA:
		status = WdfRequestRetrieveInputBuffer( Request, sizeof(INPUT_DATA), &buffer, &bufSize);
		if(!NT_SUCCESS(status)) break;

		jsData = buffer;
		pDevContext = GetDeviceContext(ControlDevContext->hParentDevice);
		copyInputData(jsData, &(pDevContext->inputs));
		break;
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);

}

VOID
copyInputData(
    IN PINPUT_DATA from,
    OUT PHID_INPUT_REPORT to
     )
/**
 * Copies input data from the given input data into the HID input report.
 * Currently these data structures are the same, but they may change in the future.
 */
{
	if(!from || !to) {
		TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "copyInputData received a null argument\n");
		return;
	}
	to->inputs.axisX = from->axisX;
	to->inputs.axisY = from->axisY;
	to->inputs.axisZ = from->axisZ;
	to->inputs.axisRX = from->axisRX;
	to->inputs.axisRY = from->axisRY;
	to->inputs.axisRZ = from->axisRZ;
	to->inputs.buttons = from->buttons & 0xFFFF; // First 16 buttons only
	return;
}

VOID
resetHidReport(
				OUT PHID_INPUT_REPORT report
			  )
{
	report->inputs.axisX = JS_RESTING_PLACE;
	report->inputs.axisY = JS_RESTING_PLACE;
	report->inputs.axisZ = JS_RESTING_PLACE;
	report->inputs.axisRX = JS_RESTING_PLACE;
	report->inputs.axisRY = JS_RESTING_PLACE;
	report->inputs.axisRZ = JS_RESTING_PLACE;
	report->inputs.buttons = 0x0;
}
