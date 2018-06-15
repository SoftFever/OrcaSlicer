/*********************************************************************

Author:     Dale Roberts
Date:       8/30/95
Program:    GIVEIO.SYS
Compile:    Use DDK BUILD facility

Purpose:    Give direct port I/O access to a user mode process.

*********************************************************************/
#include <ntddk.h>

/*
 *  The name of our device driver.
 */
#define DEVICE_NAME_STRING	L"giveio"

/*
 *  This is the "structure" of the IOPM.  It is just a simple
 * character array of length 0x2000.
 *
 *  This holds 8K * 8 bits -> 64K bits of the IOPM, which maps the
 * entire 64K I/O space of the x86 processor.  Any 0 bits will give
 * access to the corresponding port for user mode processes.  Any 1
 * bits will disallow I/O access to the corresponding port.
 */
#define	IOPM_SIZE	0x2000
typedef UCHAR IOPM[IOPM_SIZE];

/*
 *  This will hold simply an array of 0's which will be copied
 * into our actual IOPM in the TSS by Ke386SetIoAccessMap().
 * The memory is allocated at driver load time.
 */
IOPM *IOPM_local = 0;

/*
 *  These are the two undocumented calls that we will use to give
 * the calling process I/O access.
 *
 *  Ke386IoSetAccessMap() copies the passed map to the TSS.
 *
 *  Ke386IoSetAccessProcess() adjusts the IOPM offset pointer so that
 * the newly copied map is actually used.  Otherwise, the IOPM offset
 * points beyond the end of the TSS segment limit, causing any I/O
 * access by the user mode process to generate an exception.
 */
void Ke386SetIoAccessMap(int, IOPM *);
void Ke386QueryIoAccessMap(int, IOPM *);
void Ke386IoSetAccessProcess(PEPROCESS, int);

/*********************************************************************
  Release any allocated objects.
*********************************************************************/
VOID GiveioUnload(IN PDRIVER_OBJECT DriverObject)
{
	WCHAR DOSNameBuffer[] = L"\\DosDevices\\" DEVICE_NAME_STRING;
	UNICODE_STRING uniDOSString;

	if(IOPM_local)
		MmFreeNonCachedMemory(IOPM_local, sizeof(IOPM));

	RtlInitUnicodeString(&uniDOSString, DOSNameBuffer);
	IoDeleteSymbolicLink (&uniDOSString);
	IoDeleteDevice(DriverObject->DeviceObject);
}

/*********************************************************************
  Set the IOPM (I/O permission map) of the calling process so that it
is given full I/O access.  Our IOPM_local[] array is all zeros, so
the IOPM will be all zeros.  If OnFlag is 1, the process is given I/O
access.  If it is 0, access is removed.
*********************************************************************/
VOID SetIOPermissionMap(int OnFlag)
{
	Ke386IoSetAccessProcess(PsGetCurrentProcess(), OnFlag);
	Ke386SetIoAccessMap(1, IOPM_local);
}

void GiveIO(void)
{
	SetIOPermissionMap(1);
}

/*********************************************************************
  Service handler for a CreateFile() user mode call.

  This routine is entered in the driver object function call table by
the DriverEntry() routine.  When the user mode application calls
CreateFile(), this routine gets called while still in the context of
the user mode application, but with the CPL (the processor's Current
Privelege Level) set to 0.  This allows us to do kernel mode
operations.  GiveIO() is called to give the calling process I/O
access.  All the user mode application needs do to obtain I/O access
is open this device with CreateFile().  No other operations are
required.
*********************************************************************/
NTSTATUS GiveioCreateDispatch(
    IN  PDEVICE_OBJECT  DeviceObject,
    IN  PIRP            Irp
    )
{
	GiveIO();			// give the calling process I/O access

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/*********************************************************************
  Driver Entry routine.

  This routine is called only once after the driver is initially
loaded into memory.  It allocates everything necessary for the
driver's operation.  In our case, it allocates memory for our IOPM
array, and creates a device which user mode applications can open.
It also creates a symbolic link to the device driver.  This allows
a user mode application to access our driver using the \\.\giveio
notation.
*********************************************************************/
NTSTATUS DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
{
	PDEVICE_OBJECT deviceObject;
	NTSTATUS status;
	WCHAR NameBuffer[] = L"\\Device\\" DEVICE_NAME_STRING;
	WCHAR DOSNameBuffer[] = L"\\DosDevices\\" DEVICE_NAME_STRING;
	UNICODE_STRING uniNameString, uniDOSString;

	//
	//  Allocate a buffer for the local IOPM and zero it.
	//
	IOPM_local = MmAllocateNonCachedMemory(sizeof(IOPM));
	if(IOPM_local == 0)
		return STATUS_INSUFFICIENT_RESOURCES;
	RtlZeroMemory(IOPM_local, sizeof(IOPM));

	//
	//  Set up device driver name and device object.
	//
	RtlInitUnicodeString(&uniNameString, NameBuffer);
	RtlInitUnicodeString(&uniDOSString, DOSNameBuffer);

	status = IoCreateDevice(DriverObject, 0,
					&uniNameString,
					FILE_DEVICE_UNKNOWN,
					0, FALSE, &deviceObject);

	if(!NT_SUCCESS(status))
		return status;

	status = IoCreateSymbolicLink (&uniDOSString, &uniNameString);

	if (!NT_SUCCESS(status))
		return status;

    //
    //  Initialize the Driver Object with driver's entry points.
	// All we require are the Create and Unload operations.
    //
    DriverObject->MajorFunction[IRP_MJ_CREATE] = GiveioCreateDispatch;
	DriverObject->DriverUnload = GiveioUnload;
    return STATUS_SUCCESS;
}

