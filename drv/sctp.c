/*
 * Copyright (c) 2008 CO-CONV, Corp. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ntifs.h>
#include <ntstrsafe.h>
#include <wdmsec.h>

#include <ndis.h>

#include <tdi.h>
#include <tdiinfo.h>
#include <tdikrnl.h>
#include <tdistat.h>

#include "eventlog.h"
#include "eventrace_drv.h"
#include "sctp.tmh"

#define IPDevice L"\\Device\\Ip"
#define IP6Device L"\\Device\\Ip6"
#define RawIPDeviceWithSCTP L"\\Device\\RawIP\\132"
#define RawIP6DeviceWithSCTP L"\\Device\\RawIp6\\132"
#define UdpDevice L"\\Device\\Udp"
#define Udp6Device L"\\Device\\Udp6"

#define INITGUID
#include <guiddef.h>

#if NTDDI_VERSION < NTDDI_LONGHORN
#include <pfhook.h>
#include <ipexport.h>
#include <ipfirewall.h>
#include <ip6firewall.h>
#else
#include <ndis.h>
#include <fwpsk.h>
#include <fwpmk.h>
// {9CCA5AE7-D034-4c91-8871-5E6A594D29FE}
DEFINE_GUID(SCTP_ICMP_CALLOUT_V4,
    0x9cca5ae7,
    0xd034,
    0x4c91,
    0x88, 0x71, 0x5e, 0x6a, 0x59, 0x4d, 0x29, 0xfe);
// {7AB4B71A-D5EF-4b68-A66E-1D4C4481BB5B}
DEFINE_GUID(SCTP_ICMP_CALLOUT_V6,
    0x7ab4b71a,
    0xd5ef,
    0x4b68,
    0xa6, 0x6e, 0x1d, 0x4c, 0x44, 0x81, 0xbb, 0x5b);
// {269451DD-6BC3-4de8-8B5E-7E7ED432EA88}
DEFINE_GUID(SCTP_ICMP_SUBLAYER,
    0x269451dd,
    0x6bc3,
    0x4de8,
    0x8b, 0x5e, 0x7e, 0x7e, 0xd4, 0x32, 0xea, 0x88);
#endif

// {DC5B5FB4-0BD4-4b72-B744-84E137BD816E}
DEFINE_GUID(DD_SCTP_DEVICE_GUID,
0xdc5b5fb4, 0xbd4, 0x4b72, 0xb7, 0x44, 0x84, 0xe1, 0x37, 0xbd, 0x81, 0x6e);
// {082B9392-033F-43d8-9236-C115C5EC9910}
DEFINE_GUID(DD_SCTP_ONE_TO_ONE_DEVICE_GUID,
0x82b9392, 0x33f, 0x43d8, 0x92, 0x36, 0xc1, 0x15, 0xc5, 0xec, 0x99, 0x10);
// {E8F592B9-714D-4be7-A6B3-425ED65E3895}
DEFINE_GUID(DD_SCTP_ONE_TO_MANY_DEVICE_GUID,
0xe8f592b9, 0x714d, 0x4be7, 0xa6, 0xb3, 0x42, 0x5e, 0xd6, 0x5e, 0x38, 0x95);
// {E03A8439-D408-47ff-8F13-D931EA5092C0}
DEFINE_GUID(DD_SCTP_SOCKET_DEVICE_GUID,
0xe03a8439, 0xd408, 0x47ff, 0x8f, 0x13, 0xd9, 0x31, 0xea, 0x50, 0x92, 0xc0);

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/spinlock.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/poll.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/lock_profile.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/route.h>

#ifdef SCTP
#include <netinet/sctp_os.h>
#include <netinet/sctp_var.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctp_output.h>
#include <netinet6/sctp6_var.h>
#endif /* SCTP */

#include <uipc_syscalls.h>

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD Unload;

NTSTATUS StartReloadThread(VOID);
VOID StopReloadThread(VOID);

NTSTATUS OpenRawSctp(IN UCHAR, OUT HANDLE *, OUT PFILE_OBJECT *);
NTSTATUS OpenUdpSctp(IN UCHAR, IN USHORT, OUT HANDLE *, OUT PFILE_OBJECT *);
NTSTATUS SetupSctp(IN PFILE_OBJECT, IN PVOID, IN PVOID);
void CloseSctp(IN OUT HANDLE *, IN OUT PFILE_OBJECT *);
#if NTDDI_VERSION < NTDDI_LONGHORN
NTSTATUS OpenFirewall(VOID);
NTSTATUS CloseFirewall(VOID);
NTSTATUS OpenIcmpFirewall(VOID);
NTSTATUS CloseIcmpFirewall(VOID);
NTSTATUS OpenFirewall6(VOID);
void CloseFirewall6(VOID);
#else
NTSTATUS OpenEngine(VOID);
void CloseEngine(VOID);
NTSTATUS OpenIcmp(VOID);
NTSTATUS CloseIcmp(VOID);
NTSTATUS OpenIcmp6(VOID);
void CloseIcmp6(VOID);
#endif

#ifdef SCTP
int sctp_over_udp_start(void);
void sctp_over_udp_stop(void);
#endif

NTSTATUS SctpInput(IN PVOID, IN LONG, IN PVOID, IN LONG, IN PVOID, IN ULONG, IN ULONG, IN ULONG, OUT ULONG *, IN PVOID, OUT PIRP *);
NTSTATUS Sctp6Input(IN PVOID, IN LONG, IN PVOID, IN LONG, IN PVOID, IN ULONG, IN ULONG, IN ULONG, OUT ULONG *, IN PVOID, OUT PIRP *);
NTSTATUS UdpInput(IN PVOID, IN LONG, IN PVOID, IN LONG, IN PVOID, IN ULONG, IN ULONG, IN ULONG, OUT ULONG *, IN PVOID, OUT PIRP *);

IO_COMPLETION_ROUTINE IPOutputComp;
IO_COMPLETION_ROUTINE IP6OutputComp;
IO_COMPLETION_ROUTINE SctpInputComp;
IO_COMPLETION_ROUTINE Sctp6InputComp;
IO_COMPLETION_ROUTINE UdpInputComp;

#if NTDDI_VERSION < NTDDI_LONGHORN
PF_FORWARD_ACTION FirewallInput(IN unsigned char *, IN unsigned char *, IN unsigned int, IN unsigned int, IN unsigned int,
    IN IPAddr, IN IPAddr);
IPPacketFirewallPtrType FirewallIcmpInput;
IPv6FirewallHookProcType Firewall6Input;
#else
void IcmpClassify(IN const FWPS_INCOMING_VALUES0 *, IN const FWPS_INCOMING_METADATA_VALUES0 *, IN OUT void *, IN const FWPS_FILTER0 *, IN UINT64, OUT FWPS_CLASSIFY_OUT0 *);
void Icmp6Classify(IN const FWPS_INCOMING_VALUES0 *, IN const FWPS_INCOMING_METADATA_VALUES0 *, IN OUT void *, IN const FWPS_FILTER0 *, IN UINT64, OUT FWPS_CLASSIFY_OUT0 *);
NTSTATUS IcmpNotify(IN FWPS_CALLOUT_NOTIFY_TYPE, IN const GUID *, IN const FWPS_FILTER0 *);
#endif

extern void (*aio_swake)(struct socket *, struct sockbuf *);
void aio_swake_cb(struct socket *, struct sockbuf *);

__drv_dispatchType(IRP_MJ_CREATE)			DRIVER_DISPATCH SCTPCreate;
__drv_dispatchType(IRP_MJ_DEVICE_CONTROL)		DRIVER_DISPATCH SCTPDispatchDeviceControl;
__drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL)	DRIVER_DISPATCH SCTPDispatchInternalDeviceControl;
__drv_dispatchType(IRP_MJ_CLEANUP)			DRIVER_DISPATCH SCTPCleanup;
__drv_dispatchType(IRP_MJ_CLOSE)			DRIVER_DISPATCH SCTPClose;
__drv_dispatchType_other				DRIVER_DISPATCH SCTPDispatch;

static KSTART_ROUTINE ReloadThread;

MALLOC_DEFINE(M_DRV, 'dm00', "drv", "driver");
LARGE_INTEGER StartTime = {0};
#ifdef DBG
uint32_t debug_on = DEBUG_GENERIC_ERROR |
					DEBUG_KERN_ERROR |
					DEBUG_NET_ERROR |
					DEBUG_LOCK_ERROR;
#endif

PDRIVER_OBJECT SctpDriverObject = NULL;
PFILE_OBJECT SctpRawObject = NULL;
PFILE_OBJECT SctpRaw6Object = NULL;
HANDLE SctpRawHandle = NULL;
HANDLE SctpRaw6Handle = NULL;
PDEVICE_OBJECT SctpDeviceObject = NULL;
PDEVICE_OBJECT SctpSocketDeviceObject = NULL;
#if NTDDI_VERSION < NTDDI_LONGHORN
HANDLE TpIPHandle = NULL;
HANDLE TpIP6Handle = NULL;
#endif
PFILE_OBJECT aioThreadObject = NULL;
KEVENT aioEvent;
KEVENT aioTerminateEvent;
IO_CSQ aioCsq;
LIST_ENTRY aioIrpList;
KSPIN_LOCK aioLock;
KLOCK_QUEUE_HANDLE aioLockQueue;
KSPIN_LOCK Giant;
#if NTDDI_VERSION >= NTDDI_LONGHORN
HANDLE EngineHandle;
UINT32 IcmpCalloutIdV4;
UINT32 IcmpCalloutIdV6;
#endif

PFILE_OBJECT ReloadThreadObject = NULL;
KEVENT ReloadThreadEvents[2];

#if NTDDI_VERSION < NTDDI_LONGHORN
int firewall_initialized = 0;
int icmpfirewall_initialized = 0;
#endif
int domain_initialized = 0;
int mbuf_initialized = 0;
int route_initialized = 0;
int if_initialized = 0;
int sysctl_initialized = 0;
#ifdef LOCK_PROFILING
int lock_prof_initialized = 0;
#endif
#ifdef SCTP
int sctp_initialized = 0;
PFILE_OBJECT SctpUdpObject = NULL, SctpUdp6Object = NULL;
HANDLE SctpUdpHandle = NULL, SctpUdp6Handle = NULL;
PDEVICE_OBJECT SctpTdiTcpDeviceObject = NULL, SctpTdiUdpDeviceObject = NULL;
#endif

/* XXX The below variables should be moved.*/
uint16_t ip_id = 0;
int ip6_v6only = 1;
int ip6_use_deprecated = 0;
#ifdef SCTP_DEBUG
uint32_t *sctp_debug_on = &SCTP_BASE_SYSCTL(sctp_debug_on); /* XXX */
#endif


static
VOID
WriteEventLog(
    IN NTSTATUS ErrorCode,
    IN PIRP Irp,
    IN LPCWSTR Format,
    ...)
{
	NTSTATUS status = STATUS_SUCCESS;
	int error = 0;
	va_list va;
	PIO_ERROR_LOG_PACKET packet = NULL;
	PIO_STACK_LOCATION irpSp = NULL;

	va_start(va, Format);
	packet = IoAllocateErrorLogEntry(SctpDriverObject, ERROR_LOG_MAXIMUM_SIZE - 1);
	if (packet == NULL)
		error = 1;

	if (!error) {
		RtlZeroMemory(packet, ERROR_LOG_MAXIMUM_SIZE - 1);

		packet->ErrorCode = ErrorCode;
		packet->UniqueErrorValue = 0;
		packet->RetryCount = 0;
		packet->SequenceNumber = 0;
		packet->IoControlCode = 0;
		if (Irp != NULL) {
			irpSp = IoGetCurrentIrpStackLocation(Irp);
			packet->MajorFunctionCode = irpSp->MajorFunction;
			packet->FinalStatus = Irp->IoStatus.Status;
		} else {
			packet->MajorFunctionCode = 0;
			packet->FinalStatus = 0;
		}
		packet->DumpDataSize = 0;
		packet->NumberOfStrings = 1;
		packet->StringOffset = offsetof(IO_ERROR_LOG_PACKET, DumpData);
		status = RtlStringCchVPrintfW((LPWSTR)&packet->DumpData[0],
			(ERROR_LOG_MAXIMUM_SIZE - packet->StringOffset - 1) / sizeof(WCHAR),
			Format, va);

		if (!NT_SUCCESS(status)) {
			IoFreeErrorLogEntry(packet);
			error = 1;
		}
	}

	if (!error)
		IoWriteErrorLogEntry(packet);

	va_end(va);
}

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
	NTSTATUS status;
	KIRQL oldIrql;
	UNICODE_STRING devname, win_devname;
	OBJECT_ATTRIBUTES attr;
	IO_STATUS_BLOCK statusBlock;
	PIRP irp;
	PDEVICE_OBJECT deviceObject = NULL;
	int i;

	WPP_INIT_TRACING(NULL, RegistryPath);

	SctpDriverObject = DriverObject;

	KeQuerySystemTime(&StartTime);

	RtlInitUnicodeString(&devname, DD_SCTP_DEVICE_NAME);
	status = IoCreateDeviceSecure(DriverObject, 0, &devname, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
							&SDDL_DEVOBJ_SYS_ALL_ADM_ALL, &DD_SCTP_DEVICE_GUID, &SctpDeviceObject);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to create a device, code=%08x: %wZ", status, &devname);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to create a device, code=%08x: %wZ\n", status, &devname);
		goto error;
	}

	RtlInitUnicodeString(&win_devname, L"\\??\\" SCTP_DEVICE_NAME);
	status = IoCreateSymbolicLink(&win_devname, &devname);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to create a symbolic link, code=%08x: %wZ", status, &win_devname);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to create a symbolic link, code=%08x: %wZ\n", status, &win_devname);
		goto error;
	}

	status = OpenRawSctp(AF_INET, &SctpRawHandle, &SctpRawObject);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_WARN, NULL, L"Failed to open the RawIP for SCTP, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, code=%08x\n", status);
	}
	status = OpenRawSctp(AF_INET6, &SctpRaw6Handle, &SctpRaw6Object);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_WARN, NULL, L"Failed to open the RawIP6 for SCTP, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP6 for SCTP, code=%08x\n", status);
	}
	if (SctpRawHandle == NULL && SctpRaw6Handle == NULL) {
		WriteEventLog(MSG_ERROR, NULL, L"Neither RawIP nor RawIP6 exists.");
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Neither RawIP nor RawIP6 exists.\n");
		goto error;
	}

#if NTDDI_VERSION < NTDDI_LONGHORN
	if (SctpRawHandle != NULL) {
		/* Open \\Device\\Ip to get information for I/F or routing */
		RtlInitUnicodeString(&devname, IPDevice);
		InitializeObjectAttributes(&attr,
		    &devname,
		    OBJ_CASE_INSENSITIVE,
		    NULL,
		    NULL);

		status = ZwCreateFile(&TpIPHandle,
		    GENERIC_READ | GENERIC_WRITE,
		    &attr,
		    &statusBlock,
		    0L,
		    FILE_ATTRIBUTE_NORMAL,
		    FILE_SHARE_READ | FILE_SHARE_WRITE,
		    FILE_OPEN_IF,
		    0L,
		    NULL,
		    0);

		if (!NT_SUCCESS(status)) {
			WriteEventLog(MSG_ERROR, NULL, L"Failed to open a device, code=%08x: %wZ", status, &devname);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open a device, code=%08x: %wZ\n", status, &devname);
			goto error;
		}
	}

	if (SctpRaw6Handle != NULL) {
		/* Open \\Device\\Ip6 to get information for I/F or routing */
		RtlInitUnicodeString(&devname, IP6Device);
		InitializeObjectAttributes(&attr,
		    &devname,
		    OBJ_CASE_INSENSITIVE,
		    NULL,
		    NULL);

		status = ZwCreateFile(&TpIP6Handle,
		    GENERIC_READ | GENERIC_WRITE,
		    &attr,
		    &statusBlock,
		    0L,
		    FILE_ATTRIBUTE_NORMAL,
		    FILE_SHARE_READ | FILE_SHARE_WRITE,
		    FILE_OPEN_IF,
		    0L,
		    NULL,
		    0);

		if (!NT_SUCCESS(status)) {
			WriteEventLog(MSG_ERROR, NULL, L"Failed to open a device, code=%08x: %wZ", status, &devname);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open a device, code=%08x: %wZ\n", status, &devname);
			goto error;
		}
	}
#endif

	if (LockInit() < 0) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to prepare lock");
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to prepare lock\n");
		goto error;
	}

	KeInitializeSpinLock(&Giant);

	aio_swake = aio_swake_cb;
	domaininit();
	domain_initialized++;
	mbuf_init();
	mbuf_initialized++;
	sysctl_init();
	sysctl_initialized++;
#ifdef LOCK_PROFILING
	lock_prof_init();
	lock_prof_initialized++;
#endif

#ifdef SCTP
	sctp_init();
	sctp_initialized++;
#endif
#if NTDDI_VERSION < NTDDI_LONGHORN
	route_init();
	route_initialized++;
	if_init();
	if_initialized++;
#else
	if_init();
	if_initialized++;
	route_init();
	route_initialized++;
#endif

	if (SctpRawObject != NULL) {
		status = SetupSctp(SctpRawObject, SctpInput, NULL);
		if (!NT_SUCCESS(status)) {
			CloseSctp(&SctpRawHandle, &SctpRawObject);
			WriteEventLog(MSG_ERROR, NULL, L"Failed to setup the RawIP for SCTP, code=%08x", status);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to setup the RawIP for SCTP, code=%08x\n", status);
			goto error;
		}
	}
	if (SctpRaw6Object != NULL) {
		status = SetupSctp(SctpRaw6Object, Sctp6Input, NULL);
		if (!NT_SUCCESS(status)) {
			CloseSctp(&SctpRaw6Handle, &SctpRaw6Object);
			WriteEventLog(MSG_ERROR, NULL, L"Failed to setup the RawIP6 for SCTP, code=%08x", status);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to setup the RawIP6 for SCTP, code=%08x\n", status);
			goto error;
		}
	}

	StartReloadThread();

#if NTDDI_VERSION >= NTDDI_LONGHORN
	status = OpenEngine();
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to open the firewall engine, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the firewall engine, code=%08x\n", status);
		goto error;
	}
#endif

	if (SctpRawHandle != NULL) {
#if NTDDI_VERSION < NTDDI_LONGHORN
		/* Try to use IpFilterDriver to capture SCTP and ICMP packets. */
		status = OpenFirewall();
		if (NT_SUCCESS(status)) {
			firewall_initialized++;
		} else {
			/* Failed. Ok, we switch to Firewall-Hook to capture ICMP packets. */
			status = OpenIcmpFirewall();
			if (NT_SUCCESS(status)) {
				icmpfirewall_initialized++;
				WriteEventLog(MSG_WARN, NULL, L"SctpDrv cannot process SCTP if Windows Firewall works."
				    L" Please use UDP Encapsulation.");
				DebugPrint(DEBUG_GENERIC_WARN, "[sctp] SctpDrv cannot process SCTP if Windows Firewall works."
				    " Please use UDP Encapsulation.\n");
			} else {
				WriteEventLog(MSG_WARN, NULL, L"SctpDrv cannot process both SCTP and ICMP correctly."
				    L" Please use UDP Encapsulation.");
				DebugPrint(DEBUG_GENERIC_WARN, "[sctp] SctpDrv cannot process both SCTP and ICMP correctly."
				    " Please use UDP Encapsulation.\n");
			}
		}
#else
		status = OpenIcmp();
		if (!NT_SUCCESS(status)) {
			WriteEventLog(MSG_WARN, NULL, L"SctpDrv cannot process ICMP correctly.");
			DebugPrint(DEBUG_GENERIC_WARN, "[sctp] SctpDrv cannot process ICMP correctly.\n");
		}
#endif
	}

	if (SctpRaw6Handle != NULL) {
#if NTDDI_VERSION < NTDDI_LONGHORN
		status = OpenFirewall6();
#else
		status = OpenIcmp6();
#endif
		if (!NT_SUCCESS(status)) {
			WriteEventLog(MSG_WARN, NULL, L"SctpDrv cannot process ICMP6 correctly.");
			DebugPrint(DEBUG_GENERIC_WARN, "[sctp] SctpDrv cannot process ICMP6 correctly.\n");
		}
	}

#ifdef SCTP
	RtlInitUnicodeString(&devname, DD_SCTP_ONE_TO_ONE_DEVICE_NAME);
	status = IoCreateDeviceSecure(DriverObject, 0, &devname, FILE_DEVICE_NETWORK, FILE_DEVICE_SECURE_OPEN, FALSE,
						&SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX, &DD_SCTP_ONE_TO_ONE_DEVICE_GUID, &SctpTdiTcpDeviceObject);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to create a device, code=%08x: %wZ", status, &devname);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to create a device, code=%08x: %wZ\n", status, &devname);
		goto error;
	}

	RtlInitUnicodeString(&devname, DD_SCTP_ONE_TO_MANY_DEVICE_NAME);
	status = IoCreateDeviceSecure(DriverObject, 0, &devname, FILE_DEVICE_NETWORK, FILE_DEVICE_SECURE_OPEN, FALSE,
					&SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX, &DD_SCTP_ONE_TO_MANY_DEVICE_GUID, &SctpTdiUdpDeviceObject);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to create a device, code=%08x: %wZ", status, &devname);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to create a device, code=%08x: %wZ\n", status, &devname);
		goto error;
	}

	RtlInitUnicodeString(&devname, DD_SCTP_SOCKET_DEVICE_NAME);
	status = IoCreateDeviceSecure(DriverObject, 0, &devname, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
					&SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX, &DD_SCTP_SOCKET_DEVICE_GUID,&SctpSocketDeviceObject);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to create a device, code=%08x: %wZ", status, &devname);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to create a device, code=%08x: %wZ\n", status, &devname);
		goto error;
	}

	RtlInitUnicodeString(&win_devname, L"\\??\\" SCTP_SOCKET_DEVICE_NAME);
	status = IoCreateSymbolicLink(&win_devname, &devname);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to create a symbolic link, code=%08x: %wZ", status, &win_devname);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to create a symbolic link, code=%08x: %wZ\n", status, &win_devname);
		goto error;
	}
#endif

	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		DriverObject->MajorFunction[i] = SCTPDispatch;
	}
	DriverObject->MajorFunction[IRP_MJ_CREATE] = SCTPCreate;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SCTPDispatchDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = SCTPDispatchInternalDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] = SCTPCleanup;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = SCTPClose;
	DriverObject->DriverUnload = Unload;

	return STATUS_SUCCESS;
error:

	Unload(DriverObject);
	return status;
}

VOID
Unload(
    IN PDRIVER_OBJECT DriverObject)
{
	NTSTATUS status;
	UNICODE_STRING win_devname;
	int error = 0;
#ifdef SCTP
	struct sctp_inpcb *inp;
#endif

	DebugPrint(DEBUG_GENERIC_INFO, "sctpdrv.sys: Try to unload.\n");
#ifdef SCTP
	if (sctp_initialized) {
		LIST_FOREACH(inp, &SCTP_BASE_INFO(listhead), sctp_list) {
			sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_ABORT, SCTP_CALLED_DIRECTLY_NOCMPSET);
		}
		sctp_finish();
	}
#endif
#ifdef LOCK_PROFILING
	if (lock_prof_initialized) {
		lock_prof_destroy();
	}
#endif
	if (sysctl_initialized) {
		sysctl_destroy();
	}
	if (mbuf_initialized) {
		mbuf_destroy();
	}
	if (domain_initialized) {
		domaindestroy();
	}

	/* Make sure that all the timer handlers finished */
	KeFlushQueuedDpcs();

	if (if_initialized) { /* XXX */
		if_destroy();
	}
	if (route_initialized) { /* XXX */
		route_destroy();
	}
	LockDestroy();
	StopReloadThread();

	if (SctpRawHandle != NULL) {
#if NTDDI_VERSION < NTDDI_LONGHORN
		if (firewall_initialized) {
			CloseFirewall();
		}
		if (icmpfirewall_initialized) {
			CloseIcmpFirewall();
		}
#else
		CloseIcmp();
#endif
	}
	if (SctpRaw6Handle != NULL) {
#if NTDDI_VERSION < NTDDI_LONGHORN
		CloseFirewall6();
#else
		CloseIcmp6();
#endif
	}
#if NTDDI_VERSION >= NTDDI_LONGHORN
	if (EngineHandle != NULL) {
		CloseEngine();
	}
#endif

#ifdef SCTP
	if (SctpDeviceObject != NULL) {
		IoDeleteDevice(SctpDeviceObject);
		RtlInitUnicodeString(&win_devname, L"\\??\\" SCTP_DEVICE_NAME);
		IoDeleteSymbolicLink(&win_devname);
	}

	if (SctpTdiTcpDeviceObject != NULL) {
		IoDeleteDevice(SctpTdiTcpDeviceObject);
	}
	if (SctpTdiUdpDeviceObject != NULL) {
		IoDeleteDevice(SctpTdiUdpDeviceObject);
	}
#endif
	if (SctpSocketDeviceObject != NULL) {
		IoDeleteDevice(SctpSocketDeviceObject);
		RtlInitUnicodeString(&win_devname, L"\\??\\" SCTP_SOCKET_DEVICE_NAME);
		IoDeleteSymbolicLink(&win_devname);
	}

#if NTDDI_VERSION < NTDDI_LONGHORN
	if (TpIPHandle != NULL) {
		ZwClose(TpIPHandle);
	}
	if (TpIP6Handle != NULL) {
		ZwClose(TpIP6Handle);
	}
#endif

	if (SctpRawHandle != NULL) {
		CloseSctp(&SctpRawHandle, &SctpRawObject);
	}
	if (SctpRaw6Handle != NULL) {
		CloseSctp(&SctpRaw6Handle, &SctpRaw6Object);
	}

	SctpDriverObject = NULL;
	WPP_CLEANUP(NULL);
}

static
VOID
ReloadThread(
    IN PVOID context)
{
	NTSTATUS status = STATUS_SUCCESS;
	NTSTATUS waitStatus = STATUS_SUCCESS;
	PVOID events[2];

	events[0] = &ReloadThreadEvents[0];
	events[1] = &ReloadThreadEvents[1];

	for (;;) {
		waitStatus = KeWaitForMultipleObjects(2, events, WaitAny,
		    Executive, KernelMode, FALSE, NULL, NULL);
		if (waitStatus != STATUS_WAIT_1) {
			break;
		}

#ifdef SCTP
		if (SctpUdpHandle != NULL) {
			CloseSctp(&SctpUdpHandle, &SctpUdpObject);
			DebugPrint(DEBUG_GENERIC_INFO, "[sctp] UDP closed.\n");
		}

		if (SCTP_BASE_SYSCTL(sctp_udp_tunneling_port) == 0) {
			continue;
		}

		status = OpenUdpSctp(AF_INET, (uint16_t)SCTP_BASE_SYSCTL(sctp_udp_tunneling_port),
		    &SctpUdpHandle, &SctpUdpObject);
		if (!NT_SUCCESS(status)) {
			SCTP_BASE_SYSCTL(sctp_udp_tunneling_port) = 0;
			continue;
		}

		status = SetupSctp(SctpUdpObject, UdpInput, NULL);
		if (!NT_SUCCESS(status)) {
			WriteEventLog(MSG_ERROR, NULL, L"Failed to setup the UDP, port=%hu, code=%08x.",
			    (uint16_t)SCTP_BASE_SYSCTL(sctp_udp_tunneling_port), status);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to setup the UDP, port=%hu, code=%08x.",
			    (uint16_t)SCTP_BASE_SYSCTL(sctp_udp_tunneling_port), status);
			SCTP_BASE_SYSCTL(sctp_udp_tunneling_port) = 0;
			continue;
		}

		DebugPrint(DEBUG_GENERIC_INFO, "[sctp] UDP opened, port=%hu.\n",
		    (uint16_t)SCTP_BASE_SYSCTL(sctp_udp_tunneling_port));
#endif
	}
#ifdef SCTP
	if (SctpUdpHandle != NULL) {
		CloseSctp(&SctpUdpHandle, &SctpUdpObject);
		DebugPrint(DEBUG_GENERIC_INFO, "[sctp] UDP closed.\n");
	}
#endif

	PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
StartReloadThread(VOID)
{
	NTSTATUS status = STATUS_SUCCESS;
	OBJECT_ATTRIBUTES attr;
	HANDLE hThread = NULL;

	if (ReloadThreadObject != NULL) {
		status = STATUS_INVALID_PARAMETER;
		WriteEventLog(MSG_ERROR, NULL, L"Dup request for starting a thread to manage UDP.");
		DebugPrint(DEBUG_GENERIC_ERROR, "Dup request for starting a thread to manage UDP.\n");
		return status;
	}

	KeInitializeEvent(&ReloadThreadEvents[0], SynchronizationEvent, FALSE);
	KeInitializeEvent(&ReloadThreadEvents[1], SynchronizationEvent, TRUE);

	InitializeObjectAttributes(&attr,
	    NULL,
	    OBJ_KERNEL_HANDLE,
	    NULL,
	    NULL);

	status = PsCreateSystemThread(&hThread,
	    0L,
	    &attr,
	    NULL,
	    NULL,
	    ReloadThread,
	    NULL);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to start a thread to manage UDP, code=%08x.", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "Failed to start a thread to manage UDP, code=%08x\n.", status);
		return status;
	}

	ObReferenceObjectByHandle(hThread,
	    THREAD_ALL_ACCESS,
	    NULL,
	    KernelMode,
	    (PVOID *)&ReloadThreadObject,
	    NULL);
	ZwClose(hThread);

	if (ReloadThreadObject == NULL) {
		status = STATUS_INVALID_PARAMETER;
		WriteEventLog(MSG_ERROR, NULL, L"Failed to get a object to manage the thread, code=%08x.", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "Failed to get a object to manage the thread, code=%08x.\n", status);
		return status;
	}
	status = STATUS_SUCCESS;

	DebugPrint(DEBUG_GENERIC_INFO, "[sctp] UDP managing thread started.\n");

	return status;
}

VOID
StopReloadThread(VOID)
{
	if (ReloadThreadObject != NULL) {
		KeSetEvent(ReloadThreadEvents, IO_NO_INCREMENT, FALSE);
		KeWaitForSingleObject(ReloadThreadObject, Executive, KernelMode, FALSE, NULL);
		ObDereferenceObject(ReloadThreadObject);
		ReloadThreadObject = NULL;
		DebugPrint(DEBUG_GENERIC_INFO, "[sctp] UDP managing thread stopped.\n");
	}
}


NTSTATUS
OpenRawSctp(
    IN UCHAR Family,
    OUT HANDLE *pHandle,
    OUT PFILE_OBJECT *ppObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	PFILE_FULL_EA_INFORMATION eaInfo = NULL;
	int eaLength;

	OBJECT_ATTRIBUTES attr;
	IO_STATUS_BLOCK statusBlock;
	UNICODE_STRING devname;

	TCP_REQUEST_SET_INFORMATION_EX *tcp_req = NULL;
	ULONG hdrIncl = 1;
	ULONG Pktinfo = 1;
	ULONG ProtocolLevel = 10; /* PROTECTION_LEVEL_UNRESTRICTED */

	PTA_IP_ADDRESS taAddress;
	PTA_IP6_ADDRESS taAddress6;

	tcp_req = (TCP_REQUEST_SET_INFORMATION_EX *)malloc(sizeof(*tcp_req) + sizeof(hdrIncl), M_DRV, M_ZERO);
	if (tcp_req == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, family=%hhu, cause=No memory", Family);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, family=%hhu, cause=No memory\n", Family);
		goto done;
	}

	switch (Family) {
	case AF_INET:
		eaLength = sizeof(FILE_FULL_EA_INFORMATION) +
		    sizeof(TdiTransportAddress) + sizeof(TA_IP_ADDRESS);
		eaInfo = (PFILE_FULL_EA_INFORMATION)malloc(eaLength, M_DRV, M_ZERO);
		if (eaInfo == NULL) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, family=%hhu, cause=No memory", Family);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, family=%hhu, cause=No memory\n", Family);
			goto done;
		}
		RtlZeroMemory(eaInfo, eaLength);

		eaInfo->EaNameLength = TDI_TRANSPORT_ADDRESS_LENGTH;
		RtlCopyMemory(eaInfo->EaName, TdiTransportAddress, sizeof(TdiTransportAddress));
		eaInfo->EaValueLength = sizeof(TA_IP_ADDRESS);

		taAddress = (PTA_IP_ADDRESS)(eaInfo->EaName + sizeof(TdiTransportAddress));
		taAddress->TAAddressCount = 1;
		taAddress->Address[0].AddressLength = sizeof(TDI_ADDRESS_IP);
		taAddress->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;

		RtlInitUnicodeString(&devname, RawIPDeviceWithSCTP);

		break;
	case AF_INET6:
		eaLength = sizeof(FILE_FULL_EA_INFORMATION) +
		    sizeof(TdiTransportAddress) +
		    sizeof(TA_IP6_ADDRESS);
		eaInfo = (PFILE_FULL_EA_INFORMATION)malloc(eaLength, M_DRV, M_ZERO);
		if (eaInfo == NULL) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, family=%hhu, cause=No memory", Family);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, family=%hhu, cause=No memory\n", Family);
			goto done;
		}
		RtlZeroMemory(eaInfo, eaLength);

		eaInfo->EaNameLength = TDI_TRANSPORT_ADDRESS_LENGTH;
		RtlCopyMemory(eaInfo->EaName, TdiTransportAddress, sizeof(TdiTransportAddress));
		eaInfo->EaValueLength = sizeof(TA_IP6_ADDRESS);

		taAddress6 = (PTA_IP6_ADDRESS)(eaInfo->EaName +
		    sizeof(TdiTransportAddress));
		taAddress6->TAAddressCount = 1;
		taAddress6->Address[0].AddressLength = sizeof(TDI_ADDRESS_IP6);
		taAddress6->Address[0].AddressType = TDI_ADDRESS_TYPE_IP6;

		RtlInitUnicodeString(&devname, RawIP6DeviceWithSCTP);

		break;
	default:
		status = STATUS_INVALID_PARAMETER;
		WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, family=%hhu, cause=Invalid family", Family);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, family=%hhu, cause=Invalid family\n", Family);
		goto done;
	}

	InitializeObjectAttributes(&attr,
	    &devname,
	    OBJ_CASE_INSENSITIVE,
	    NULL,
	    NULL);

	status = ZwCreateFile(pHandle,
	    GENERIC_READ | GENERIC_WRITE,
	    &attr,
	    &statusBlock,
	    0L,
	    FILE_ATTRIBUTE_NORMAL,
	    FILE_SHARE_READ | FILE_SHARE_WRITE,
	    FILE_OPEN_IF,
	    0L,
	    eaInfo,
	    eaLength);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, family=%hhu, cause=Failure to create, code=%08x", Family, status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, family=%hhu, cause=Failure to create, code=%08x\n", Family, status);
		goto done;
	}

	status = ObReferenceObjectByHandle(*pHandle,
	    GENERIC_READ | GENERIC_WRITE,
	    NULL,
	    KernelMode,
	    ppObject,
	    NULL);
	if (!NT_SUCCESS(status)) {
		ZwClose(*pHandle);
		*pHandle = NULL;
		WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, family=%hhu, cause=Failure to get object, code=%08x", Family, status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, family=%hhu, cause=Failure to get object, code=%08x\n", Family, status);
		goto done;
	}

	switch (Family) {
	case AF_INET:
		tcp_req->ID.toi_entity.tei_entity = CL_TL_ENTITY;
		tcp_req->ID.toi_entity.tei_instance = 0;
		tcp_req->ID.toi_class = INFO_CLASS_PROTOCOL;
		tcp_req->ID.toi_type = INFO_TYPE_ADDRESS_OBJECT;
		tcp_req->ID.toi_id = 12; /* IP_HDRINCL */
		RtlCopyMemory(&tcp_req->Buffer, &hdrIncl, sizeof(hdrIncl));
		tcp_req->BufferSize = sizeof(hdrIncl);

		status = ZwDeviceIoControlFile(*pHandle,
		    NULL,
		    NULL,
		    NULL,
		    &statusBlock,
		    IOCTL_TCP_SET_INFORMATION_EX,
		    tcp_req,
		    sizeof(*tcp_req) + sizeof(hdrIncl),
		    NULL,
		    0);
		if (!NT_SUCCESS(status)) {
			ObDereferenceObject(*ppObject);
			*ppObject = NULL;
			ZwClose(*pHandle);
			*pHandle = NULL;
			WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, cause=Failure to set IP_HDRINCL, code=%08x", status);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, cause=Failure to set IP_HDRINCL, code=%08x\n", status);
			goto done;
		}

		break;

	case AF_INET6:
		tcp_req->ID.toi_entity.tei_entity = CL_TL_ENTITY;
		tcp_req->ID.toi_entity.tei_instance = 0;
		tcp_req->ID.toi_class = INFO_CLASS_PROTOCOL;
		tcp_req->ID.toi_type = INFO_TYPE_ADDRESS_OBJECT;

		tcp_req->ID.toi_id = 12; /* IPV6_HDRINCL */
		RtlCopyMemory(&tcp_req->Buffer, &hdrIncl, sizeof(hdrIncl));
		tcp_req->BufferSize = sizeof(hdrIncl);

		status = ZwDeviceIoControlFile(*pHandle,
		    NULL,
		    NULL,
		    NULL,
		    &statusBlock,
		    IOCTL_TCP_SET_INFORMATION_EX,
		    tcp_req,
		    sizeof(*tcp_req) + sizeof(hdrIncl),
		    NULL,
		    0);
		if (!NT_SUCCESS(status)) {
			ObDereferenceObject(*ppObject);
			*ppObject = NULL;
			ZwClose(*pHandle);
			*pHandle = NULL;
			WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, cause=Failure to set IPV6_HDRINCL, code=%08x", status);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, cause=Failure to set IPV6_HDRINCL, code=%08x\n", status);
			goto done;
		}

		tcp_req->ID.toi_id = 27; /* IPV6_PKTINFO */
		RtlCopyMemory(&tcp_req->Buffer, &Pktinfo, sizeof(Pktinfo));
		tcp_req->BufferSize = sizeof(Pktinfo);

		status = ZwDeviceIoControlFile(*pHandle,
		    NULL,
		    NULL,
		    NULL,
		    &statusBlock,
		    IOCTL_TCP_SET_INFORMATION_EX,
		    tcp_req,
		    sizeof(*tcp_req) + sizeof(hdrIncl),
		    NULL,
		    0);
		if (!NT_SUCCESS(status)) {
			ObDereferenceObject(*ppObject);
			*ppObject = NULL;
			ZwClose(*pHandle);
			*pHandle = NULL;
			WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, cause=Failure to set IPV6_PKTINFO, code=%08x", status);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, cause=Failure to set IPV6_PKTINFO, code=%08x\n", status);
			goto done;
		}

		tcp_req->ID.toi_id = 38; /* IPV6_PROTECTION_LEVEL */
		RtlCopyMemory(&tcp_req->Buffer, &ProtocolLevel, sizeof(ProtocolLevel));
		tcp_req->BufferSize = sizeof(ProtocolLevel);

		status = ZwDeviceIoControlFile(*pHandle,
		    NULL,
		    NULL,
		    NULL,
		    &statusBlock,
		    IOCTL_TCP_SET_INFORMATION_EX,
		    tcp_req,
		    sizeof(*tcp_req) + sizeof(hdrIncl),
		    NULL,
		    0);
		if (!NT_SUCCESS(status)) {
			ObDereferenceObject(*ppObject);
			*ppObject = NULL;
			ZwClose(*pHandle);
			*pHandle = NULL;
			WriteEventLog(MSG_ERROR, NULL, L"Failed to open the RawIP for SCTP, cause=Failure to set IPV6_PROTECTION_LEVEL, code=%08x", status);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] Failed to open the RawIP for SCTP, cause=Failure to set IPV6_PROTECTION_LEVEL, code=%08x\n", status);
			goto done;
		}
		break;

	default:
		break;
	}

done:
	if (tcp_req != NULL)
		free(tcp_req, M_DRV);

	if (eaInfo != NULL)
		free(eaInfo, M_DRV);

	return status;
}

NTSTATUS
OpenUdpSctp(
    IN UCHAR Family,
    IN USHORT Port,
    OUT HANDLE *pHandle,
    OUT PFILE_OBJECT *ppObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	PFILE_FULL_EA_INFORMATION eaInfo = NULL;
	int eaLength;

	OBJECT_ATTRIBUTES attr;
	IO_STATUS_BLOCK statusBlock;
	UNICODE_STRING devname;

	TCP_REQUEST_SET_INFORMATION_EX *tcp_req = NULL;
	ULONG value = 1;

	PTA_IP_ADDRESS taAddress;
	PTA_IP6_ADDRESS taAddress6;

	ASSERT(pHandle != NULL && ppObject != NULL);
	if (pHandle == NULL || ppObject == NULL) {
		status = STATUS_INVALID_PARAMETER;
		goto done;
	}
	ASSERT(*pHandle == NULL && *ppObject == NULL);
	if (*pHandle != NULL || *ppObject != NULL) {
		status = STATUS_INVALID_PARAMETER;
		goto done;
	}

	*pHandle = NULL;
	*ppObject = NULL;

	tcp_req = (TCP_REQUEST_SET_INFORMATION_EX *)malloc(sizeof(*tcp_req) + sizeof(value), M_DRV, M_ZERO);
	if (tcp_req == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		WriteEventLog(MSG_ERROR, NULL, L"[OpenUdpSctp] No memory.");
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenUdpSctp] No memory.\n");
		goto done;
	}

	switch (Family) {
	case AF_INET:
		eaLength = sizeof(FILE_FULL_EA_INFORMATION) +
		    sizeof(TdiTransportAddress) + sizeof(TA_IP_ADDRESS);
		eaInfo = (PFILE_FULL_EA_INFORMATION)malloc(eaLength, M_DRV, M_ZERO);
		if (eaInfo == NULL) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			WriteEventLog(MSG_ERROR, NULL, L"[OpenUdpSctp] No memory.");
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenUdpSctp] No memory.\n");
			goto done;
		}

		eaInfo->EaNameLength = TDI_TRANSPORT_ADDRESS_LENGTH;
		RtlCopyMemory(eaInfo->EaName, TdiTransportAddress, sizeof(TdiTransportAddress));
		eaInfo->EaValueLength = sizeof(TA_IP_ADDRESS);

		taAddress = (PTA_IP_ADDRESS)(eaInfo->EaName + sizeof(TdiTransportAddress));
		taAddress->TAAddressCount = 1;
		taAddress->Address[0].AddressLength = sizeof(TDI_ADDRESS_IP);
		taAddress->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
		taAddress->Address[0].Address[0].sin_port = htons(Port);

		RtlInitUnicodeString(&devname, UdpDevice);

		break;
	case AF_INET6:
		eaLength = sizeof(FILE_FULL_EA_INFORMATION) +
		    sizeof(TdiTransportAddress) +
		    sizeof(TA_IP6_ADDRESS);
		eaInfo = (PFILE_FULL_EA_INFORMATION)malloc(eaLength, M_DRV, M_ZERO);
		if (eaInfo == NULL) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			WriteEventLog(MSG_ERROR, NULL, L"[OpenUdpSctp] No memory");
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenUdpSctp] No memory\n");
			goto done;
		}

		eaInfo->EaNameLength = TDI_TRANSPORT_ADDRESS_LENGTH;
		RtlCopyMemory(eaInfo->EaName, TdiTransportAddress, sizeof(TdiTransportAddress));
		eaInfo->EaValueLength = sizeof(TA_IP6_ADDRESS);

		taAddress6 = (PTA_IP6_ADDRESS)(eaInfo->EaName +
		    sizeof(TdiTransportAddress));
		taAddress6->TAAddressCount = 1;
		taAddress6->Address[0].AddressLength = sizeof(TDI_ADDRESS_IP6);
		taAddress6->Address[0].AddressType = TDI_ADDRESS_TYPE_IP6;
		taAddress6->Address[0].Address[0].sin6_port = htons(Port);

		RtlInitUnicodeString(&devname, Udp6Device);

		break;
	default:
		status = STATUS_INVALID_PARAMETER;
		goto done;
	}

	InitializeObjectAttributes(&attr,
	    &devname,
	    OBJ_CASE_INSENSITIVE,
	    NULL,
	    NULL);

	status = ZwCreateFile(pHandle,
	    GENERIC_READ | GENERIC_WRITE,
	    &attr,
	    &statusBlock,
	    0L,
	    FILE_ATTRIBUTE_NORMAL,
	    FILE_SHARE_READ | FILE_SHARE_WRITE,
	    FILE_OPEN_IF,
	    0L,
	    eaInfo,
	    eaLength);
	if (!NT_SUCCESS(status)) {
		*pHandle = NULL;
		WriteEventLog(MSG_ERROR, NULL, L"[OpenUdpSctp] Failed to create, code=%08x, family=%hhu, port=%hhu",
		    status, Family, Port);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenUdpSctp] Failed to create, code=%08x, family=%hhu, port=%hhu\n",
		    status, Family, Port);
		goto done;
	}

	status = ObReferenceObjectByHandle(*pHandle,
	    GENERIC_READ | GENERIC_WRITE,
	    NULL,
	    KernelMode,
	    ppObject,
	    NULL);
	if (!NT_SUCCESS(status)) {
		ZwClose(*pHandle);
		*pHandle = NULL;
		*ppObject = NULL;
		WriteEventLog(MSG_ERROR, NULL, L"[OpenUdpSctp] Failed to get object, code=%08x, family=%hhu, port=%hhu",
		    status, Family, Port);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenUdpSctp] Failed to get object, code=%08x, family=%hhu, port=%hhu\n",
		    status, Family, Port);
		goto done;
	}

	tcp_req->ID.toi_entity.tei_entity = CL_TL_ENTITY;
	tcp_req->ID.toi_entity.tei_instance = 0;
	tcp_req->ID.toi_class = INFO_CLASS_PROTOCOL;
	tcp_req->ID.toi_type = INFO_TYPE_ADDRESS_OBJECT;
	switch (Family) {
	case AF_INET:
		tcp_req->ID.toi_id = 27; /* IP_PKTINFO */
		value = 1;
		RtlCopyMemory(&tcp_req->Buffer, &value, sizeof(value));
		tcp_req->BufferSize = sizeof(value);

		status = ZwDeviceIoControlFile(*pHandle,
		    NULL,
		    NULL,
		    NULL,
		    &statusBlock,
		    IOCTL_TCP_SET_INFORMATION_EX,
		    tcp_req,
		    sizeof(*tcp_req) + sizeof(value),
		    NULL,
		    0);
		if (!NT_SUCCESS(status)) {
			ObDereferenceObject(*ppObject);
			*ppObject = NULL;
			ZwClose(*pHandle);
			*pHandle = NULL;
			WriteEventLog(MSG_ERROR, NULL, L"[OpenUdpSctp] Failed to set IP_PKTINFO, code=%08x, port=%hu",
			    status, Port);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenUdpSctp] Failed to set IP_PKTINFO, code=%08x, port=%hu\n",
			    status, Port);
			goto done;
		}

		break;

	case AF_INET6:
		tcp_req->ID.toi_id = 27; /* IPV6_PKTINFO */
		value = 1;
		RtlCopyMemory(&tcp_req->Buffer, &value, sizeof(value));
		tcp_req->BufferSize = sizeof(value);

		status = ZwDeviceIoControlFile(*pHandle,
		    NULL,
		    NULL,
		    NULL,
		    &statusBlock,
		    IOCTL_TCP_SET_INFORMATION_EX,
		    tcp_req,
		    sizeof(*tcp_req) + sizeof(value),
		    NULL,
		    0);
		if (!NT_SUCCESS(status)) {
			ObDereferenceObject(*ppObject);
			*ppObject = NULL;
			ZwClose(*pHandle);
			*pHandle = NULL;
			WriteEventLog(MSG_ERROR, NULL, L"[OpenUdpSctp] Failed to set IPV6_PKTINFO, code=%08x, port=%hu",
			    status, Port);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenUdpSctp] Failed to set IPV6_PKTINFO, code=%08x, port=%hu\n",
			    status, Port);
			goto done;
		}

		tcp_req->ID.toi_id = 38; /* IPV6_PROTECTION_LEVEL */
		value = 10; /* PROTECTION_LEVEL_UNRESTRICTED */
		RtlCopyMemory(&tcp_req->Buffer, &value, sizeof(value));
		tcp_req->BufferSize = sizeof(value);

		status = ZwDeviceIoControlFile(*pHandle,
		    NULL,
		    NULL,
		    NULL,
		    &statusBlock,
		    IOCTL_TCP_SET_INFORMATION_EX,
		    tcp_req,
		    sizeof(*tcp_req) + sizeof(value),
		    NULL,
		    0);
		if (!NT_SUCCESS(status)) {
			ObDereferenceObject(*ppObject);
			*ppObject = NULL;
			ZwClose(*pHandle);
			*pHandle = NULL;
			WriteEventLog(MSG_ERROR, NULL, L"[OpenUdpSctp] Failed to set IPV6_PROTECTION_LEVEL, code=%08x, port=%hu",
			    status, Port);
			DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenUdpSctp] Failed to set IPV6_PROTECTION_LEVEL, code=%08x, port=%hu\n",
			    status, Port);
			goto done;
		}
		break;

	default:
		break;
	}

done:
	if (tcp_req != NULL)
		free(tcp_req, M_DRV);

	if (eaInfo != NULL)
		free(eaInfo, M_DRV);

	return status;
}

NTSTATUS
SetupSctp(
    IN PFILE_OBJECT pObject,
    IN PVOID eventHandler,
    IN PVOID eventContext)
{
	NTSTATUS status;
	PDEVICE_OBJECT deviceObject;
	PIRP irp;

	deviceObject = IoGetRelatedDeviceObject(pObject);
	irp = TdiBuildInternalDeviceControlIrp(TDI_SET_EVENT_HANDLER,
	    deviceObject,
	    pObject,
	    NULL,
	    NULL);
	TdiBuildSetEventHandler(irp,
	    deviceObject,
	    pObject,
	    NULL,
	    NULL,
	    TDI_EVENT_RECEIVE_DATAGRAM,
	    eventHandler,
	    eventContext);

	status = IoCallDriver(deviceObject, irp);
	return status;
}

void
CloseSctp(
    IN OUT HANDLE *handlePtr,
    IN OUT PFILE_OBJECT *pObjectPtr)
{
	if (pObjectPtr != NULL && (*pObjectPtr) != NULL) {
		ObDereferenceObject(*pObjectPtr);
		*pObjectPtr = NULL;
	}
	if (handlePtr != NULL && (*handlePtr) != NULL) {
		ZwClose(*handlePtr);
		*handlePtr = NULL;
	}
}


#if NTDDI_VERSION < NTDDI_LONGHORN
NTSTATUS
SetFirewall(PacketFilterExtensionPtr pfep)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING usFilterName;
	PFILE_OBJECT fileObject = NULL;
	PDEVICE_OBJECT deviceObject = NULL;
	KEVENT event;
	IO_STATUS_BLOCK statusBlock;
	PF_SET_EXTENSION_HOOK_INFO psehi;
	PIRP irp = NULL;

	RtlInitUnicodeString(&usFilterName, DD_IPFLTRDRVR_DEVICE_NAME);
	status= IoGetDeviceObjectPointer(&usFilterName,
	    STANDARD_RIGHTS_ALL, &fileObject, &deviceObject);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[SetFirewall] Failed to get pointer, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [SetFirewall] Failed to get pointer, code=%08x\n", status);
		goto done;
	}

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	RtlZeroMemory(&statusBlock, sizeof(statusBlock));
	RtlZeroMemory(&psehi, sizeof(psehi));
	psehi.ExtensionPointer = pfep;
	irp = IoBuildDeviceIoControlRequest(IOCTL_PF_SET_EXTENSION_POINTER,
	    deviceObject, (PVOID)&psehi, sizeof(psehi),
	    NULL, 0, FALSE, &event, &statusBlock);
	if (irp == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		WriteEventLog(MSG_ERROR, NULL, L"[SetFirewall] No memory.");
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [SetFirewall] No memory.\n");
		goto done;
	}

	status = IoCallDriver(deviceObject, irp);
	irp = NULL;
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = statusBlock.Status;
	}
done:
	if (irp != NULL) {
		IoFreeIrp(irp);
	}
	if (fileObject != NULL) {
		ObDereferenceObject(fileObject);
	}
	return status;
}

NTSTATUS
OpenFirewall(void)
{
	NTSTATUS status = STATUS_SUCCESS;
	status = SetFirewall(FirewallInput);
	return status;
}

NTSTATUS
CloseFirewall(void)
{
	NTSTATUS status = STATUS_SUCCESS;
	status = SetFirewall(NULL);
	return status;
}


NTSTATUS
OpenIcmpFirewall(void)
{
	NTSTATUS status;
	IP_SET_FIREWALL_HOOK_INFO fwInfo;

	ASSERT(TpIPHandle != NULL);
	if (TpIPHandle == NULL) {
		return STATUS_INVALID_PARAMETER;
	}

	RtlZeroMemory(&fwInfo, sizeof(fwInfo));
	fwInfo.Add = TRUE;
	fwInfo.Priority = 1;
	fwInfo.FirewallPtr = FirewallIcmpInput;
	status = ZwDeviceIoControlFile(TpIPHandle,
	    NULL,
	    NULL,
	    NULL,
	    NULL,
	    IOCTL_IP_SET_FIREWALL_HOOK,
	    &fwInfo,
	    sizeof(fwInfo),
	    NULL,
	    0);

	return status;
}

NTSTATUS
CloseIcmpFirewall(void)
{
	NTSTATUS status;
	IP_SET_FIREWALL_HOOK_INFO fwInfo;

	ASSERT(TpIPHandle != NULL);
	if (TpIPHandle == NULL) {
		return STATUS_INVALID_PARAMETER;
	}

	RtlZeroMemory(&fwInfo, sizeof(fwInfo));
	fwInfo.Add = FALSE;
	fwInfo.Priority = 1;
	fwInfo.FirewallPtr = FirewallIcmpInput;
	status = ZwDeviceIoControlFile(TpIPHandle,
	    NULL,
	    NULL,
	    NULL,
	    NULL,
	    IOCTL_IP_SET_FIREWALL_HOOK,
	    &fwInfo,
	    sizeof(fwInfo),
	    NULL,
	    0);

	return status;
}

void CloseFirewall6Comp(void);
NTSTATUS
OpenFirewall6(void)
{
	NTSTATUS status = STATUS_SUCCESS;

	IPv6DisableFirewallHook(CloseFirewall6Comp);
	status = IPv6EnableFirewallHook(Firewall6Input);
	return status;
}

void
CloseFirewall6(void)
{
	IPv6DisableFirewallHook(CloseFirewall6Comp);
}

void
CloseFirewall6Comp(void)
{
	;
}
#else
NTSTATUS
OpenEngine(void)
{
	NTSTATUS status = STATUS_SUCCESS;
	FWPM_SUBLAYER0 icmpSubLayer;
	FWPM_SESSION0 session;
	BOOLEAN inTransaction = FALSE;

	RtlZeroMemory(&session, sizeof(session));
	session.flags = FWPM_SESSION_FLAG_DYNAMIC;
	status = FwpmEngineOpen0(
	    NULL,
	    RPC_C_AUTHN_WINNT,
	    NULL,
	    &session,
	    &EngineHandle);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[OpenEngine] Failed to open, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenEngine] Failed to open, code=%08x\n", status);
		goto error;
	}

	status = FwpmTransactionBegin0(EngineHandle, 0);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[OpenEngine] Failed to transact, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenEngine] Failed to transact, code=%08x\n", status);
		goto error;
	}
	inTransaction = TRUE;

	RtlZeroMemory(&icmpSubLayer, sizeof(icmpSubLayer));
	icmpSubLayer.subLayerKey = SCTP_ICMP_SUBLAYER;
	icmpSubLayer.displayData.name = L"ICMP Sub-Layer";
	icmpSubLayer.displayData.description =
	    L"Sub-Layer for use by ICMP callouts";
	icmpSubLayer.flags = 0;
	icmpSubLayer.weight = FWP_EMPTY;

	status = FwpmSubLayerAdd0(EngineHandle, &icmpSubLayer, NULL);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[OpenEngine] Failed to add, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenEngine] Failed to add, code=%08x\n", status);
		goto error;
	}

	status = FwpmTransactionCommit0(EngineHandle);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[OpenEngine] Failed to commit, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenEngine] Failed to commit, code=%08x\n", status);
		goto error;
	}

	return STATUS_SUCCESS;
error:
	if (inTransaction) {
		FwpmTransactionAbort0(EngineHandle);
	}
	if (EngineHandle != NULL) {
		FwpmEngineClose0(EngineHandle);
		EngineHandle = NULL;
	}
	return status;
}

void
CloseEngine(void)
{
	FwpmEngineClose0(EngineHandle);
	EngineHandle = NULL;
}

NTSTATUS
OpenIcmpCommon(
    IN const GUID *layerKey,
    IN const GUID *calloutKey,
    IN uint8_t protocolNum,
    FWPS_CALLOUT_CLASSIFY_FN0 classifyFn,
    OUT UINT32 *calloutId)
{
	NTSTATUS status = STATUS_SUCCESS;
	FWPS_CALLOUT0 registerCallout;
	FWPM_CALLOUT0 addCallout;
	FWPM_FILTER0 filter;
	FWPM_FILTER_CONDITION0 filterConditions[3];
	BOOLEAN inTransaction = FALSE;

	RtlZeroMemory(&registerCallout, sizeof(registerCallout));
	registerCallout.calloutKey = *calloutKey;
	registerCallout.classifyFn = classifyFn;
	registerCallout.notifyFn = IcmpNotify;

	status = FwpsCalloutRegister0(
	    SctpDeviceObject,
	    &registerCallout,
	    calloutId);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[OpenIcmpCommon] Failed to register, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenIcmpCommon] Failed to register, code=%08x\n", status);
		return status;
	}

	status = FwpmTransactionBegin0(EngineHandle, 0);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[OpenIcmpCommon] Failed to transact, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenIcmpCommon] Failed to transact, code=%08x\n", status);
		return status;
	}
	inTransaction = TRUE;

	RtlZeroMemory(&addCallout, sizeof(addCallout));
	addCallout.calloutKey = *calloutKey;
	addCallout.displayData.name = L"ICMP Callout";
	addCallout.displayData.description = L"ICMP Callout by SCTP Driver";
	addCallout.applicableLayer = *layerKey;

	status = FwpmCalloutAdd0(
	    EngineHandle,
	    &addCallout,
	    NULL,
	    NULL);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[OpenIcmpCommon] Failed to add callout, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenIcmpCommon] Failed to add callout, code=%08x\n", status);
		goto error;
	}

	RtlZeroMemory(&filter, sizeof(filter));
	filter.layerKey = *layerKey;
	filter.displayData.name = L"ICMP Filter";
	filter.displayData.description = L"Protocol/Type for ICMP";

	filter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
	filter.action.calloutKey = *calloutKey;
	filter.filterCondition = filterConditions;
	filter.subLayerKey = SCTP_ICMP_SUBLAYER;
	filter.weight.type = FWP_EMPTY;
	filter.numFilterConditions = 2;

	RtlZeroMemory(filterConditions, sizeof(filterConditions));
	filterConditions[0].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
	filterConditions[0].matchType = FWP_MATCH_EQUAL;
	filterConditions[0].conditionValue.type = FWP_UINT8;
	filterConditions[0].conditionValue.uint16 = protocolNum;

	filterConditions[1].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
	filterConditions[1].matchType = FWP_MATCH_EQUAL;
	filterConditions[1].conditionValue.type = FWP_UINT16;
	filterConditions[1].conditionValue.uint16 = ICMP_UNREACH;

	status = FwpmFilterAdd0(
	    EngineHandle,
	    &filter,
	    NULL,
	    NULL);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[OpenIcmpCommon] Failed to add filter, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenIcmpCommon] Failed to add filter, code=%08x\n", status);
		goto error;
	}

	status = FwpmTransactionCommit0(EngineHandle);
	if (!NT_SUCCESS(status)) {
		WriteEventLog(MSG_ERROR, NULL, L"[OpenIcmpCommon] Failed to commit, code=%08x", status);
		DebugPrint(DEBUG_GENERIC_ERROR, "[sctp] [OpenIcmpCommon] Failed to commit, code=%08x\n", status);
		goto error;
	}

	return STATUS_SUCCESS;
error:
	if (inTransaction) {
		FwpmTransactionAbort0(EngineHandle);
	}
	return status;
}

void
CloseIcmpCommon(
    UINT32 calloutId)
{
	if (calloutId != 0) {
		FwpsCalloutUnregisterById0(calloutId);
	}
}

NTSTATUS
OpenIcmp(void)
{
	NTSTATUS status = STATUS_SUCCESS;

	status = OpenIcmpCommon(&FWPM_LAYER_INBOUND_TRANSPORT_V4, &SCTP_ICMP_CALLOUT_V4, IPPROTO_ICMP,
	    IcmpClassify, &IcmpCalloutIdV4);
	return status;
}

NTSTATUS
CloseIcmp(void)
{
	NTSTATUS status = STATUS_SUCCESS;
	CloseIcmpCommon(IcmpCalloutIdV4);
	return status;
}


NTSTATUS
OpenIcmp6(void)
{
	NTSTATUS status = STATUS_SUCCESS;

	status = OpenIcmpCommon(&FWPM_LAYER_INBOUND_TRANSPORT_V6, &SCTP_ICMP_CALLOUT_V6, IPPROTO_ICMPV6,
	    Icmp6Classify, &IcmpCalloutIdV6);
	return status;
}

void
CloseIcmp6(void)
{
	CloseIcmpCommon(IcmpCalloutIdV6);
}
#endif


typedef struct _IPOutputCtx {
	struct mbuf *o_pak;
	TDI_CONNECTION_INFORMATION sendDatagramInfo;
	TA_IP_ADDRESS address;
} IPOutputCtx;

NTSTATUS
IPOutputComp(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp,
    IN PVOID context)
{
	IPOutputCtx *ctx = context;
	PMDL mdl = NULL, nextMdl = NULL;

	/* Clean up Irp and Mdl before buffer free. */
	if (irp->MdlAddress != NULL) {
		for (mdl = irp->MdlAddress; mdl != NULL; mdl = nextMdl) {
			nextMdl = mdl->Next;
			MmUnlockPages(mdl);
			IoFreeMdl(mdl);
		}

		irp->MdlAddress = NULL;
	}

	IoFreeIrp(irp);

	if (ctx != NULL) {
		if (ctx->o_pak != NULL) {
			m_freem(ctx->o_pak);
		}
		free(ctx, M_DRV);
	}


	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
IPOutput(
    IN struct mbuf *o_pak,
    IN struct route *ro)
{
	NTSTATUS status;
	PDEVICE_OBJECT deviceObject;
	PIRP irp;
	struct mbuf *m;
	ULONG totalLength = 0;
	PMDL top = NULL, prevMdl = NULL, nextMdl = NULL, mdl;
	IPOutputCtx *ctx = NULL;
	struct ip *ip;
	struct sockaddr_in *dst;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "IPOutput - enter\n");

	if (o_pak == NULL || ro == NULL) {
		status = STATUS_INVALID_PARAMETER;
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IPOutput - leave #1\n");
		goto error;
	}
	totalLength = o_pak->m_pkthdr.len;

	ip = mtod(o_pak, struct ip *);
	dst = (struct sockaddr_in *)&ro->ro_dst;
	if (ro->ro_rt == NULL) {
		dst->sin_family = AF_INET;
		dst->sin_addr = ip->ip_dst;
		rtalloc(ro);
		if (ro->ro_rt == NULL) {
			status = STATUS_INVALID_PARAMETER;
			DebugPrint(DEBUG_GENERIC_VERBOSE, "IPOutput - leave #2\n");
			goto error;
		}
	}

	ctx = (IPOutputCtx *)malloc(sizeof(IPOutputCtx), M_DRV, M_ZERO);
	if (ctx == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IPOutput - leave #3\n");
		goto error;
	}

	ctx->o_pak = o_pak;
	ctx->sendDatagramInfo.RemoteAddressLength = sizeof(TA_IP_ADDRESS);
	ctx->sendDatagramInfo.RemoteAddress = &ctx->address;
	ctx->address.TAAddressCount = 1;
	ctx->address.Address[0].AddressLength = sizeof(TDI_ADDRESS_IP);
	ctx->address.Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
	if (((struct sockaddr_in *)&ro->ro_rt->rt_gateway)->sin_addr.s_addr != INADDR_ANY) {
		RtlCopyMemory(&ctx->address.Address[0].Address[0].in_addr,
		    &((struct sockaddr_in *)&ro->ro_rt->rt_gateway)->sin_addr, sizeof(struct in_addr));
	} else {
		RtlCopyMemory(&ctx->address.Address[0].Address[0].in_addr,
		    &((struct sockaddr_in *)&ro->ro_dst)->sin_addr, sizeof(struct in_addr));
	}


	deviceObject = IoGetRelatedDeviceObject(SctpRawObject);
	irp = IoAllocateIrp(deviceObject->StackSize + 1, FALSE);
	if (irp == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IPOutput - leave #4\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto error;
	}

	for (m = o_pak; m != NULL; m = m->m_next) {
		mdl = IoAllocateMdl(mtod(m, caddr_t), m->m_len, FALSE, FALSE, NULL);
		if (mdl == NULL) {
			DebugPrint(DEBUG_GENERIC_VERBOSE, "IPOutput - leave #5\n");
			goto error;
		}
		NDIS_BUFFER_LINKAGE((PNDIS_BUFFER)mdl) = NULL;

		__try {
			MmProbeAndLockPages(mdl, KernelMode, IoModifyAccess);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			IoFreeMdl(mdl);
			DebugPrint(DEBUG_GENERIC_VERBOSE, "IPOutput - leave #6\n");
			goto error;
		}
		if (top == NULL)
			top = mdl;

		if (prevMdl != NULL)
			NDIS_BUFFER_LINKAGE((PNDIS_BUFFER)prevMdl) = mdl;

		prevMdl = mdl;
	}

	TdiBuildSendDatagram(irp,
	    deviceObject,
	    SctpRawObject,
	    IPOutputComp,
	    ctx,
	    top,
	    totalLength,
	    &ctx->sendDatagramInfo);

	status = IoCallDriver(deviceObject, irp);
	DebugPrint(DEBUG_GENERIC_VERBOSE, "IPOutput - leave\n");
	return status;

error:
	if (top != NULL) {
		for (mdl = top; mdl != NULL; mdl = nextMdl) {
			nextMdl = mdl->Next;
			MmUnlockPages(mdl);
			IoFreeMdl(mdl);
		}
		top = NULL;
	}

	if (ctx != NULL)
		free(ctx, M_DRV);

	if (o_pak != NULL)
		m_freem(o_pak);

	return status;
}


typedef struct _IP6OutputCtx {
	struct mbuf *o_pak;
	TDI_CONNECTION_INFORMATION sendDatagramInfo;
	TA_IP6_ADDRESS address;
} IP6OutputCtx;

NTSTATUS
IP6OutputComp(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp,
    IN PVOID context)
{
	IP6OutputCtx *ctx = context;
	PMDL mdl = NULL, nextMdl = NULL;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6OutputComp - enter\n");

	/* Clean up Irp and Mdl before buffer free. */
	if (irp->MdlAddress != NULL) {
		for (mdl = irp->MdlAddress; mdl != NULL; mdl = nextMdl) {
			nextMdl = mdl->Next;
			MmUnlockPages(mdl);
			IoFreeMdl(mdl);
		}

		irp->MdlAddress = NULL;
	}

	IoFreeIrp(irp);

	if (ctx != NULL) {
		if (ctx->o_pak != NULL) {
			m_freem(ctx->o_pak);
		}
		free(ctx, M_DRV);
	}

	DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6OutputComp - leave\n");
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
IP6Output(
    IN struct mbuf *o_pak,
    IN struct route *ro)
{
	NTSTATUS status;
	PDEVICE_OBJECT deviceObject;
	PIRP irp;
	struct mbuf *m;
	ULONG totalLength = 0;
	PMDL top = NULL, prevMdl = NULL, nextMdl = NULL, mdl;
	IP6OutputCtx *ctx = NULL;
	struct ip6_hdr *ip6;
	struct sockaddr_in6 *dest, dst, src;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6Output - enter\n");

	if (o_pak == NULL || ro == NULL) {
		status = STATUS_INVALID_PARAMETER;
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6Output - leave#1\n");
		goto error;
	}
	totalLength = o_pak->m_pkthdr.len;

	ip6 = mtod(o_pak, struct ip6_hdr *);
	dest = (struct sockaddr_in6 *)&ro->ro_dst;
	if (ro->ro_rt == NULL) {
		dest->sin6_family = AF_INET6;
		RtlCopyMemory(&dest->sin6_addr, &ip6->ip6_dst, sizeof(struct in6_addr));
		rtalloc(ro);
		if (ro->ro_rt == NULL) {
			status = STATUS_INVALID_PARAMETER;
			DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6Output - leave#2\n");
			goto error;
		}
	}

	RtlZeroMemory(&src, sizeof(src));
	src.sin6_family = AF_INET6;
	in6_recoverscope(&src, &ip6->ip6_dst, NULL);
	ip6->ip6_src = src.sin6_addr;

	RtlZeroMemory(&dst, sizeof(dst));
	dst.sin6_family = AF_INET6;
	in6_recoverscope(&dst, &ip6->ip6_dst, NULL);
	ip6->ip6_dst = dst.sin6_addr;
	ip6->ip6_plen = htons(o_pak->m_pkthdr.len - sizeof(*ip6));

	ctx = (IP6OutputCtx *)malloc(sizeof(IP6OutputCtx), M_DRV, M_ZERO);
	if (ctx == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6Output: leave #3\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto error;
	}

	ctx->o_pak = o_pak;
	ctx->sendDatagramInfo.RemoteAddressLength = sizeof(TA_IP6_ADDRESS);
	ctx->sendDatagramInfo.RemoteAddress = &ctx->address;
	ctx->address.TAAddressCount = 1;
	ctx->address.Address[0].AddressLength = sizeof(TDI_ADDRESS_IP6);
	ctx->address.Address[0].AddressType = TDI_ADDRESS_TYPE_IP6;
	if (!IN6ADDR_ISUNSPECIFIED((const SOCKADDR_IN6 *)&ro->ro_rt->rt_gateway)) {
		RtlCopyMemory(&ctx->address.Address[0].Address[0].sin6_addr,
		    &((struct sockaddr_in6 *)&ro->ro_rt->rt_gateway)->sin6_addr, sizeof(struct in6_addr));
	} else {
		RtlCopyMemory(&ctx->address.Address[0].Address[0].sin6_addr,
		    &dst.sin6_addr, sizeof(struct in6_addr));
	}
	ctx->address.Address[0].Address[0].sin6_scope_id = dst.sin6_scope_id;

	deviceObject = IoGetRelatedDeviceObject(SctpRaw6Object);
	irp = IoAllocateIrp(deviceObject->StackSize + 1, FALSE);
	if (irp == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6Output: leave #4\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto error;
	}

	for (m = o_pak; m != NULL; m = m->m_next) {
		mdl = IoAllocateMdl(mtod(m, caddr_t), m->m_len, FALSE, FALSE, NULL);
		if (mdl == NULL) {
			DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6Output: leave #5\n");
			goto error;
		}
		NDIS_BUFFER_LINKAGE((PNDIS_BUFFER)mdl) = NULL;

		__try {
			MmProbeAndLockPages(mdl, KernelMode, IoModifyAccess);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			IoFreeMdl(mdl);
			DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6Output: leave #6\n");
			goto error;
		}
		if (top == NULL) {
			top = mdl;
		}
		if (prevMdl != NULL) {
			NDIS_BUFFER_LINKAGE((PNDIS_BUFFER)prevMdl) = mdl;
		}
		prevMdl = mdl;
	}

	TdiBuildSendDatagram(irp,
	    deviceObject,
	    SctpRaw6Object,
	    IP6OutputComp,
	    ctx,
	    top,
	    totalLength,
	    &ctx->sendDatagramInfo);

	status = IoCallDriver(deviceObject, irp);
	DebugPrint(DEBUG_GENERIC_VERBOSE, "IP6Output: leave\n");
	return status;

error:
	if (top != NULL) {
		for (mdl = top; mdl != NULL; mdl = nextMdl) {
			nextMdl = mdl->Next;
			MmUnlockPages(mdl);
			IoFreeMdl(mdl);
		}
		top = NULL;
	}
	if (ctx != NULL) {
		free(ctx, M_DRV);
	}
	if (o_pak != NULL) {
		m_freem(o_pak);
	}

	return status;
}

NTSTATUS
SctpInputComp(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp,
    IN PVOID context)
{
	struct mbuf *m = context;
	PMDL mdl = NULL, nextMdl = NULL;
	struct ip *ip;
	struct ifnet *ifp = NULL;
	struct ifaddr *ifa = NULL;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInputComp - enter\n");

	/* Clean up Irp and Mdl before buffer free. */
	if (irp->MdlAddress != NULL) {
		for (mdl = irp->MdlAddress; mdl != NULL; mdl = nextMdl) {
			nextMdl = mdl->Next;
			MmUnlockPages(mdl);
			IoFreeMdl(mdl);
		}

		irp->MdlAddress = NULL;
	}

	IoFreeIrp(irp);

	if (m != NULL) {
		ip = mtod(m, struct ip *);
		IFNET_WLOCK();
		TAILQ_FOREACH(ifp, &ifnet, if_link) {
			IF_LOCK(ifp);
			TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
				if (ifa->ifa_addr->sa_family == AF_INET &&
				    ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr == ip->ip_dst.s_addr) {
					break;
				}
			}
			IF_UNLOCK(ifp);
			if (ifa != NULL) {
				break;
			}
		}
		if (ifp != NULL) {
			IFREF(ifp);
		}
		IFNET_WUNLOCK();
		m->m_pkthdr.rcvif = ifp;

		switch (ip->ip_p) {
#ifdef SCTP
		case IPPROTO_SCTP:
			sctp_input(m, sizeof(struct ip));
			break;
#endif
		default:
			m_freem(m);
		}
	}

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInputComp - leave\n");
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "SctpInputComp: irql=%u,oldIrql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "SctpInputComp: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
SctpInput(
    IN PVOID tdiEventContext,
    IN LONG sourceAddressLength,
    IN PVOID sourceAddress,
    IN LONG optionsLength,
    IN PVOID options,
    IN ULONG receiveDatagramFlags,
    IN ULONG bytesIndicated,
    IN ULONG bytesAvailable,
    OUT ULONG *bytesTaken,
    IN PVOID tsdu,
    OUT PIRP *ioRequestPacket)
{
	PDEVICE_OBJECT deviceObject;
	unsigned int i;
	struct mbuf *m;
	struct ip *ip = NULL;
	PIRP irp = NULL;
	PMDL mdl = NULL;
	struct ifnet *ifp = NULL;
	struct ifaddr *ifa = NULL;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInput - enter\n");

	if (bytesAvailable < sizeof(struct ip)) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInput - leave#1\n");
		return STATUS_DATA_NOT_ACCEPTED;
	}

	m = m_getm2(NULL, bytesAvailable, M_DONTWAIT, MT_DATA, M_PKTHDR | M_EOR);
	if (m == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInput - leave#2\n");
		return STATUS_DATA_NOT_ACCEPTED;
	}
	if ((m->m_flags & M_EOR) == 0) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInput - leave#3\n");
		return STATUS_DATA_NOT_ACCEPTED;
	}

	m->m_len = m->m_pkthdr.len = bytesAvailable;
	if (bytesAvailable == bytesIndicated) {
		RtlCopyMemory(m->m_data, tsdu, bytesAvailable);

		ip = mtod(m, struct ip *);
		IFNET_WLOCK();
		TAILQ_FOREACH(ifp, &ifnet, if_link) {
			IF_LOCK(ifp);
			TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
				if (ifa->ifa_addr->sa_family == AF_INET &&
				    ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr == ip->ip_dst.s_addr) {
					break;
				}
			}
			IF_UNLOCK(ifp);
			if (ifa != NULL) {
				break;
			}
		}
		if (ifp != NULL) {
			IFREF(ifp);
		}
		IFNET_WUNLOCK();
		m->m_pkthdr.rcvif = ifp;

		switch (ip->ip_p) {
#ifdef SCTP
		case IPPROTO_SCTP:
			sctp_input(m, sizeof(struct ip));
			break;
#endif
		default:
			m_freem(m);
			break;
		}

		*bytesTaken = bytesAvailable;
#ifdef DBG
		if (KeGetCurrentIrql() != oldIrql) {
			DebugPrint(DEBUG_LOCK_ERROR, "SctpInput: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
			DbgBreakPoint();
		}
		if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
			DebugPrint(DEBUG_LOCK_ERROR, "SctpInput: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
			DbgBreakPoint();
		}
#endif
		DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInput - leave#3\n");
		return STATUS_SUCCESS;
	}

	deviceObject = IoGetRelatedDeviceObject(SctpRawObject);
	irp = IoAllocateIrp(deviceObject->StackSize + 1, FALSE);
	if (irp == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInput - leave#6\n");
		goto error;
	}

	mdl = IoAllocateMdl(m->m_data, bytesAvailable, FALSE, FALSE, NULL);
	if (mdl == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInput - leave#7\n");
		goto error;
	}

	__try {
		MmProbeAndLockPages(mdl, KernelMode, IoModifyAccess);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInput - leave#8\n");
		IoFreeMdl(mdl);
		mdl = NULL;
		goto error;
	}

	TdiBuildReceiveDatagram(irp,
	    deviceObject,
	    SctpRawObject,
	    SctpInputComp,
	    m,
	    mdl,
	    bytesAvailable,
	    0,
	    NULL,
	    TDI_RECEIVE_NORMAL);

	IoSetNextIrpStackLocation(irp);

	*bytesTaken = 0;
	*ioRequestPacket = irp;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SctpInput - leave\n");
	return STATUS_MORE_PROCESSING_REQUIRED;

error:
	if (mdl != NULL) {
		MmUnlockPages(mdl);
		IoFreeMdl(mdl);
		mdl = NULL;
	}

	if (irp != NULL) {
		IoFreeIrp(irp);
		irp = NULL;
	}

	if (m != NULL) {
		m_freem(m);
	}

#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "SctpInput#2: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "SctpInput#2: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return STATUS_DATA_NOT_ACCEPTED;
}


NTSTATUS
Sctp6InputComp(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp,
    IN PVOID context)
{
	struct mbuf *m = context;
	int off = 0;
	struct ip6_hdr *ip6h;
	struct ifnet *ifp = NULL;
	struct sockaddr_in6 src, dst;
	struct in6_pktinfo *pktinfo = NULL;
	PMDL mdl = NULL, nextMdl = NULL;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6InputComp - enter\n");

	/* Clean up Irp and Mdl before buffer free. */
	if (irp->MdlAddress != NULL) {
		for (mdl = irp->MdlAddress; mdl != NULL; mdl = nextMdl) {
			nextMdl = mdl->Next;
			MmUnlockPages(mdl);
			IoFreeMdl(mdl);
		}

		irp->MdlAddress = NULL;
	}

	IoFreeIrp(irp);

	if (m != NULL) {
		off = sizeof(struct ip6_hdr);
		ip6h = mtod(m, struct ip6_hdr *);
		switch (ip6h->ip6_nxt) {
#ifdef SCTP
		case IPPROTO_SCTP:
			sctp6_input(&m, &off, IPPROTO_SCTP);
			break;
#endif
		default:
			break;
		}
	}

	DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6InputComp - leave\n");
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "Sctp6InputComp: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "Sctp6InputComp: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
Sctp6Input(
    IN PVOID tdiEventContext,
    IN LONG sourceAddressLength,
    IN PVOID sourceAddress,
    IN LONG optionsLength,
    IN PVOID options,
    IN ULONG receiveDatagramFlags,
    IN ULONG bytesIndicated,
    IN ULONG bytesAvailable,
    OUT ULONG *bytesTaken,
    IN PVOID tsdu,
    OUT PIRP *ioRequestPacket)
{
	PDEVICE_OBJECT deviceObject;
	unsigned int i;
	struct ip6_hdr *ip6h;
	struct mbuf *m;
	int off = 0;
	PIRP irp = NULL;
	PMDL mdl = NULL;
	struct ifnet *ifp = NULL;
	struct sockaddr_in6 src, dst;
	struct in6_pktinfo *pktinfo = NULL;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6Input - enter\n");

	if (sourceAddressLength != sizeof(TA_IP6_ADDRESS) ||
	    optionsLength < sizeof(TDI_CMSGHDR) ||
	    ((PTDI_CMSGHDR)options)->cmsg_len != TDI_CMSG_LEN(sizeof(struct in6_pktinfo)) ||
	    ((PTDI_CMSGHDR)options)->cmsg_level != IPPROTO_IPV6 ||
	    ((PTDI_CMSGHDR)options)->cmsg_type != IPV6_PKTINFO) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6Input - leave#1\n");
		return STATUS_DATA_NOT_ACCEPTED;
	}

	pktinfo = (struct in6_pktinfo *)TDI_CMSG_DATA(options);

	m = m_getm2(NULL, sizeof(struct ip6_hdr) + bytesAvailable, M_DONTWAIT, MT_DATA, M_PKTHDR | M_EOR);
	if (m == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6Input - leave#2\n");
		return STATUS_DATA_NOT_ACCEPTED;
	}
	if ((m->m_flags & M_EOR) == 0) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6Input - leave#3\n");
		return STATUS_DATA_NOT_ACCEPTED;
	}
	ip6h = mtod(m, struct ip6_hdr *);
	RtlZeroMemory(ip6h, sizeof(struct ip6_hdr));

	ip6h->ip6_vfc = (IPV6_VERSION & IPV6_VERSION_MASK);
	ip6h->ip6_hlim = 255;
	ip6h->ip6_plen = htons((USHORT)bytesAvailable);
	ip6h->ip6_nxt = IPPROTO_SCTP;

	RtlCopyMemory(&ip6h->ip6_src, &((PTA_IP6_ADDRESS)sourceAddress)->Address[0].Address[0].sin6_addr,
	    sizeof(struct in6_addr));
	RtlCopyMemory(&ip6h->ip6_dst, &pktinfo->ipi6_addr,
	    sizeof(struct in6_addr));

	RtlZeroMemory(&src, sizeof(src));
	src.sin6_family = AF_INET6;
	RtlCopyMemory(&src.sin6_addr, &ip6h->ip6_src, sizeof(struct in6_addr));
	src.sin6_scope_id = pktinfo->ipi6_ifindex;
	in6_embedscope(&ip6h->ip6_src, &src);

	RtlZeroMemory(&dst, sizeof(dst));
	dst.sin6_family = AF_INET6;
	RtlCopyMemory(&dst.sin6_addr, &ip6h->ip6_dst, sizeof(struct in6_addr));
	dst.sin6_scope_id = pktinfo->ipi6_ifindex;
	in6_embedscope(&ip6h->ip6_dst, &dst);

	IFNET_WLOCK();
	TAILQ_FOREACH(ifp, &ifnet, if_link) {
		if (ifp->if_ifIndex == pktinfo->ipi6_ifindex) {
			break;
		}
	}
	if (ifp != NULL) {
		IFREF(ifp);
	}
	IFNET_WUNLOCK();

	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = sizeof(struct ip6_hdr) + bytesAvailable;

	if (bytesAvailable == bytesIndicated) {
		RtlCopyMemory(m->m_data + sizeof(struct ip6_hdr), tsdu, bytesAvailable);

		off += sizeof(struct ip6_hdr);

		switch (ip6h->ip6_nxt) {
#ifdef SCTP
		case IPPROTO_SCTP:
			sctp6_input(&m, &off, IPPROTO_SCTP);
			break;
#endif
		default:
			break;
		}

		*bytesTaken = bytesAvailable;
#ifdef DBG
		if (KeGetCurrentIrql() != oldIrql) {
			DebugPrint(DEBUG_LOCK_ERROR, "Sctp6Input: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
			DbgBreakPoint();
		}
		if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
			DebugPrint(DEBUG_LOCK_ERROR, "Sctp6Input: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
			DbgBreakPoint();
		}
#endif
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6Input - leave#3\n");
		return STATUS_SUCCESS;
	}

	deviceObject = IoGetRelatedDeviceObject(SctpRaw6Object);

	irp = IoAllocateIrp(deviceObject->StackSize + 1, FALSE);
	if (irp == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6Input - leave#6\n");
		goto error;
	}

	mdl = IoAllocateMdl(m->m_data + sizeof(struct ip6_hdr), bytesAvailable, FALSE, FALSE, NULL);
	if (mdl == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6Input - leave#7\n");
		goto error;
	}

	__try {
		MmProbeAndLockPages(mdl, KernelMode, IoModifyAccess);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		IoFreeMdl(mdl);
		mdl = NULL;
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6Input - leave#8\n");
		goto error;
	}

	TdiBuildReceiveDatagram(irp,
	    deviceObject,
	    SctpRaw6Object,
	    Sctp6InputComp,
	    m,
	    mdl,
	    bytesAvailable,
	    0,
	    NULL,
	    TDI_RECEIVE_NORMAL);

	IoSetNextIrpStackLocation(irp);

	*bytesTaken = 0;
	*ioRequestPacket = irp;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "Sctp6Input - leave\n");
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "Sctp6Input#3: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "Sctp6Input#3: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return STATUS_MORE_PROCESSING_REQUIRED;

error:
	if (mdl != NULL) {
		MmUnlockPages(mdl);
		IoFreeMdl(mdl);
		mdl = NULL;
	}

	if (irp != NULL) {
		IoFreeIrp(irp);
		irp = NULL;
	}

	if (m != NULL) {
		m_freem(m);
	}
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "Sctp6Input#3: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "Sctp6Input#3: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return STATUS_DATA_NOT_ACCEPTED;
}


#ifdef SCTP
NTSTATUS
UdpInputComp(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp,
    IN PVOID context)
{
	struct mbuf *m = context;
	PMDL mdl = NULL, nextMdl = NULL;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInputComp - enter\n");

	/* Clean up Irp and Mdl before buffer free. */
	if (irp->MdlAddress != NULL) {
		for (mdl = irp->MdlAddress; mdl != NULL; mdl = nextMdl) {
			nextMdl = mdl->Next;
			MmUnlockPages(mdl);
			IoFreeMdl(mdl);
		}

		irp->MdlAddress = NULL;
	}

	IoFreeIrp(irp);

	if (m != NULL) {
		sctp_input_with_port(m, sizeof(struct ip), m->m_pkthdr.udp_sport);
	}

	DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInputComp - leave\n");
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "UdpInputComp: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "UdpInputComp: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
UdpInput(
    IN PVOID tdiEventContext,
    IN LONG sourceAddressLength,
    IN PVOID sourceAddress,
    IN LONG optionsLength,
    IN PVOID options,
    IN ULONG receiveDatagramFlags,
    IN ULONG bytesIndicated,
    IN ULONG bytesAvailable,
    OUT ULONG *bytesTaken,
    IN PVOID tsdu,
    OUT PIRP *ioRequestPacket)
{
	unsigned int i;
	struct ip *ip;
	struct mbuf *m;
	struct in_pktinfo *pktinfo = NULL;
	struct ifnet *ifp = NULL;
	struct ifaddr *ifa = NULL;
	PDEVICE_OBJECT deviceObject;
	PIRP irp = NULL;
	PMDL mdl = NULL;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInput - enter\n");

	if (sourceAddressLength != sizeof(TA_IP_ADDRESS) ||
	    sourceAddress == NULL ||
	    optionsLength < sizeof(TDI_CMSGHDR) ||
	    options == NULL ||
	    ((PTDI_CMSGHDR)options)->cmsg_len != TDI_CMSG_LEN(sizeof(struct in_pktinfo)) ||
	    ((PTDI_CMSGHDR)options)->cmsg_level != IPPROTO_IP ||
	    ((PTDI_CMSGHDR)options)->cmsg_type != IP_PKTINFO) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInput - leave#1\n");
		return STATUS_DATA_NOT_ACCEPTED;
	}

	pktinfo = (struct in_pktinfo *)TDI_CMSG_DATA(options);

	m = m_getm2(NULL, sizeof(struct ip) + bytesAvailable, M_DONTWAIT, MT_DATA, M_PKTHDR | M_EOR);
	if (m == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInput - leave#2\n");
		return STATUS_DATA_NOT_ACCEPTED;
	}
	if ((m->m_flags & M_EOR) == 0) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInput - leave#3\n");
		return STATUS_DATA_NOT_ACCEPTED;
	}
	ip = mtod(m, struct ip *);
	RtlZeroMemory(ip, sizeof(struct ip));

	ip->ip_v = IPVERSION;
	ip->ip_len = htons(sizeof(struct ip) + (USHORT)bytesAvailable);
	ip->ip_ttl = 0xff;
	ip->ip_p = IPPROTO_SCTP;

	RtlCopyMemory(&ip->ip_src, &((PTA_IP_ADDRESS)sourceAddress)->Address[0].Address[0].in_addr,
	    sizeof(struct in_addr));
	RtlCopyMemory(&ip->ip_dst, &pktinfo->ipi_addr,
	    sizeof(struct in_addr));

	m->m_pkthdr.udp_sport = ((PTA_IP_ADDRESS)sourceAddress)->Address[0].Address[0].sin_port;
	m->m_pkthdr.udp_dport = htons(SCTP_BASE_SYSCTL(sctp_udp_tunneling_port));

	IFNET_WLOCK();
	TAILQ_FOREACH(ifp, &ifnet, if_link) {
		IF_LOCK(ifp);
		TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family == AF_INET &&
			    ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr == ip->ip_dst.s_addr) {
				break;
			}
		}
		IF_UNLOCK(ifp);
		if (ifa != NULL) {
			break;
		}
	}
	if (ifp != NULL) {
		IFREF(ifp);
	}
	IFNET_WUNLOCK();

	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = sizeof(struct ip) + bytesAvailable;

	if (bytesAvailable == bytesIndicated) {
		RtlCopyMemory(m->m_data + sizeof(struct ip), tsdu, bytesAvailable);

		sctp_input_with_port(m, sizeof(struct ip), m->m_pkthdr.udp_sport);

		*bytesTaken = bytesAvailable;
#ifdef DBG
		if (KeGetCurrentIrql() != oldIrql) {
			DebugPrint(DEBUG_LOCK_ERROR, "UdpInput: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
			DbgBreakPoint();
		}
		if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
			DebugPrint(DEBUG_LOCK_ERROR, "UdpInput: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
			DbgBreakPoint();
		}
#endif
		DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInput - leave#4\n");
		return STATUS_SUCCESS;
	}

	deviceObject = IoGetRelatedDeviceObject(SctpUdpObject);

	irp = IoAllocateIrp(deviceObject->StackSize + 1, FALSE);
	if (irp == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInput - leave#5\n");
		goto error;
	}

	mdl = IoAllocateMdl(m->m_data + sizeof(struct ip), bytesAvailable, FALSE, FALSE, NULL);
	if (mdl == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInput - leave#6\n");
		goto error;
	}

	__try {
		MmProbeAndLockPages(mdl, KernelMode, IoModifyAccess);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		IoFreeMdl(mdl);
		mdl = NULL;
		DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInput - leave#7\n");
		goto error;
	}

	TdiBuildReceiveDatagram(irp,
	    deviceObject,
	    SctpUdpObject,
	    UdpInputComp,
	    m,
	    mdl,
	    bytesAvailable,
	    0,
	    NULL,
	    TDI_RECEIVE_NORMAL);

	IoSetNextIrpStackLocation(irp);

	*bytesTaken = 0;
	*ioRequestPacket = irp;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "UdpInput - leave\n");
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "UdpInput#2: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "UdpInput#2: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return STATUS_MORE_PROCESSING_REQUIRED;

error:
	if (mdl != NULL) {
		MmUnlockPages(mdl);
		IoFreeMdl(mdl);
		mdl = NULL;
	}

	if (irp != NULL) {
		IoFreeIrp(irp);
		irp = NULL;
	}

	if (m != NULL) {
		m_freem(m);
	}
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "UdpInput#3: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "UdpInput#3: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return STATUS_DATA_NOT_ACCEPTED;
}
#endif


#if NTDDI_VERSION < NTDDI_LONGHORN
void
icmp_input(struct mbuf *m, int off)
{
	struct ip *ip = NULL;
	struct icmp *icmp = NULL;
	int icmplen = 0;
	int cmd = 0;
	struct sockaddr_in sin;
#ifdef SCTP
	struct sockaddr_in from, to;
	struct sctphdr *sh = NULL;
	uint32_t vrf_id;
	struct sctp_inpcb *inp = NULL;
	struct sctp_tcb *stcb = NULL;
	struct sctp_nets *net = NULL;
#endif
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	ip = mtod(m, struct ip *);
	if (ip->ip_p != IPPROTO_ICMP) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp_input - leave#1\n");
		goto done;
	}

	icmp = (struct icmp *)(m->m_data + off);
	icmplen = ntohs(ip->ip_len);
	if (m->m_pkthdr.len < icmplen) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp_input - leave#2\n");
		goto done;
	}

	if (icmplen < 8 + sizeof(struct ip)) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp_input - leave#3\n");
		goto done;
	}

#ifdef SCTP
	if (icmp->icmp_ip.ip_p == IPPROTO_SCTP) {
		if (icmplen < 8 + (icmp->icmp_ip.ip_hl << 2) + sizeof(struct sctphdr)) {
			DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp_input - leave#4\n");
			goto done;
		}

		sh = (struct sctphdr *)(((caddr_t)&icmp->icmp_ip) + (icmp->icmp_ip.ip_hl << 2));

		RtlZeroMemory(&from, sizeof(from));
		RtlZeroMemory(&to, sizeof(to));

		to.sin_family = AF_INET;
		to.sin_port = sh->src_port;
		to.sin_addr = icmp->icmp_ip.ip_src;

		from.sin_family = AF_INET;
		from.sin_port = sh->dest_port;
		from.sin_addr = ip->ip_src;

		vrf_id = SCTP_DEFAULT_VRFID;

#if 0
		SCTPDBG_ADDR(SCTP_DEBUG_NOISY, (struct sockaddr *)&to);
		SCTPDBG_ADDR(SCTP_DEBUG_NOISY, (struct sockaddr *)&from);
#endif
		stcb = sctp_findassociation_addr_sa((struct sockaddr *)&to,
		    (struct sockaddr *)&from, &inp, &net, 1, vrf_id);
		if (stcb != NULL) {
			SCTP_TCB_UNLOCK(stcb);
		} else if (inp != NULL) {
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			SCTP_INP_WUNLOCK(inp);
		}
	}
#endif

	switch (icmp->icmp_type) {
	case ICMP_UNREACH:
		switch (icmp->icmp_code) {
		case ICMP_UNREACH_NET:
		case ICMP_UNREACH_HOST:
		case ICMP_UNREACH_SRCFAIL:
		case ICMP_UNREACH_NET_UNKNOWN:
		case ICMP_UNREACH_HOST_UNKNOWN:
		case ICMP_UNREACH_ISOLATED:
		case ICMP_UNREACH_PRECEDENCE_CUTOFF:
			cmd = PRC_UNREACH_NET;
			break;

		case ICMP_UNREACH_NEEDFRAG:
			cmd = PRC_MSGSIZE;
			break;

		case ICMP_UNREACH_PROTOCOL:
#ifdef SCTP
			if (stcb != NULL) {
				cmd = PRC_UNREACH_PORT;
			} else /* From router, invalid response */
#endif
			{
				cmd = PRC_UNREACH_NET;
			}
			break;

		case ICMP_UNREACH_NET_PROHIB:
		case ICMP_UNREACH_HOST_PROHIB:
		case ICMP_UNREACH_FILTER_PROHIB:
			cmd = PRC_UNREACH_ADMIN_PROHIB;
			break;

		default:
			DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp_input - leave5\n");
			goto done;
		}
		break;

	default:
		DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp_input- leave6\n");
		goto done;
	}

	RtlZeroMemory(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr = icmp->icmp_ip.ip_dst;

	switch (icmp->icmp_ip.ip_p) {
#ifdef SCTP
	case IPPROTO_SCTP:
		sctp_ctlinput(cmd, (struct sockaddr *)&sin, &icmp->icmp_ip);
		break;
#endif
	default:
		break;
	}
	DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp_input- leave\n");
done:
	if (m != NULL) {
		m_freem(m);
	}
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "icmp_input: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "icmp_input: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
}

void
icmp6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	int off = *offp;
	struct ip6_hdr *ip6;
	struct icmp6_hdr *icmp6;
	int icmp6len;
	struct ip6ctlparam ip6cp;
	int cmd = 0;
	struct sockaddr_in6 icmp6src, icmp6dst;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	ip6 = (struct ip6_hdr *)m->m_data;
	icmp6 = (struct icmp6_hdr *)(m->m_data + off);
	icmp6len = ntohs(ip6->ip6_plen);
	if (icmp6len < sizeof(struct icmp6_hdr)) {
                DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp6_input - leave#1\n");
                goto done;
	}

	switch (icmp6->icmp6_type) {
	case ICMP6_DST_UNREACH:
                switch (icmp6->icmp6_code) {
                case ICMP6_DST_UNREACH_NOROUTE:
			cmd = PRC_UNREACH_NET;
			break;
                case ICMP6_DST_UNREACH_ADMIN:
			cmd = PRC_UNREACH_ADMIN_PROHIB;
			break;
                case ICMP6_DST_UNREACH_BEYONDSCOPE:
			cmd = PRC_PARAMPROB;
			break;
                case ICMP6_DST_UNREACH_ADDR:
			cmd = PRC_HOSTDEAD;
			break;
                case ICMP6_DST_UNREACH_NOPORT:
			cmd = PRC_UNREACH_PORT;
			break;
                default:
                        DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp6_input - leave2\n");
                        goto done;
                }
                break;

	case ICMP6_PACKET_TOO_BIG:
		cmd = PRC_MSGSIZE;
		break;

        default:
                DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp6_input - leave3\n");
                goto done;
        }

	RtlZeroMemory(&icmp6src, sizeof(icmp6src));
	icmp6src.sin6_family = AF_INET6;
	RtlCopyMemory(&icmp6src.sin6_addr, &ip6->ip6_src, sizeof(struct in6_addr));

	RtlZeroMemory(&icmp6dst, sizeof(icmp6dst));
	icmp6dst.sin6_family = AF_INET6;
	RtlCopyMemory(&icmp6dst.sin6_addr, &ip6->ip6_dst, sizeof(struct in6_addr));

	if (m->m_pkthdr.rcvif != NULL) {
		icmp6src.sin6_scope_id = m->m_pkthdr.rcvif->if_ifIndex;
		icmp6dst.sin6_scope_id = m->m_pkthdr.rcvif->if_ifIndex;
	}

	RtlZeroMemory(&ip6cp, sizeof(ip6cp));
	ip6cp.ip6c_m = m;
	ip6cp.ip6c_icmp6 = icmp6;
	ip6cp.ip6c_ip6 = &icmp6->icmp6_ip6;
	ip6cp.ip6c_off = off + sizeof(struct icmp6_hdr);
	ip6cp.ip6c_dst = &icmp6dst;
	ip6cp.ip6c_finaldst = &icmp6dst.sin6_addr;
	ip6cp.ip6c_src = &icmp6src;
	ip6cp.ip6c_nxt = icmp6->icmp6_ip6.ip6_nxt;

	switch (icmp6->icmp6_ip6.ip6_nxt) {
#ifdef SCTP
	case IPPROTO_SCTP:
		sctp6_ctlinput(cmd, (struct sockaddr *)&icmp6dst, &ip6cp);
		break;
#endif
	default:
		break;
	}

	*offp += icmp6len;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "icmp6_input - leave\n");
done:
	if (m != NULL) {
		m_freem(m);
	}
	*mp = NULL;
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "icmp6_input: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "icmp6_input: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
}

/*
 * Firewall Input
 * If TX and RX are on the same machine, this function
 * can be called from the SCTP output path
 */
PF_FORWARD_ACTION
FirewallInput(
    IN unsigned char *packetHeader,
    IN unsigned char *packet,
    IN unsigned int packetLength,
    IN unsigned int recvInterfaceIndex,
    IN unsigned int sendInterfaceIndex,
    IN IPAddr recvLinkNextHop,
    IN IPAddr sendLinkNextHop)
{
	PF_FORWARD_ACTION ret = PF_FORWARD;
	struct mbuf *m = NULL;
	struct ip *ip = NULL;
	int off = 0, len = 0;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "FirewallInput - enter\n");

	if (recvInterfaceIndex == INVALID_PF_IF_INDEX) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "FirewallInput - leave#1\n");
		goto done;
	}

	ip = (struct ip *)packetHeader;
	if (ip->ip_p != IPPROTO_ICMP && ip->ip_p != IPPROTO_SCTP) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "FirewallInput - leave#2\n");
		goto done;
	}
	off = ip->ip_hl << 2;
	len = off + packetLength;

	m = m_getm2(NULL, len, M_DONTWAIT, MT_DATA, M_PKTHDR | M_EOR);
	if (m == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "FirewallInput - leave#3\n");
		goto done;
	}
	if ((m->m_flags & M_EOR) == 0) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "FirewallInput - leave#4\n");
		goto done;
	}

	RtlCopyMemory(m->m_data, packetHeader, off);
	RtlCopyMemory(m->m_data + off, packet, packetLength);
	m->m_len = m->m_pkthdr.len = len;

	switch (ip->ip_p) {
	case IPPROTO_ICMP:
		icmp_input(m, off);
		m = NULL;
		break;
#ifdef SCTP
	case IPPROTO_SCTP:
		sctp_input(m, off);
		m = NULL;
		ret = DROP;
		break;
#endif
	default:
		break;
	}

	DebugPrint(DEBUG_GENERIC_VERBOSE, "FirewallInput - leave\n");
done:
	if (m != NULL) {
		m_freem(m);
	}
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "FirewallInput: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
#endif
	return ret;
}


FORWARD_ACTION
FirewallIcmpInput(
    IN PVOID *ppData,
    IN UINT recvInterfaceIndex,
    IN UINT *pSendInterfaceIndex,
    IN UCHAR *pDestinationType,
    IN PVOID pContext,
    IN UINT contextLength,
    IN IPRcvBuf **ppRcvBuf)
{
	struct IPRcvBuf *rcvBuf, *rcvBuf0 = (struct IPRcvBuf *)*ppData;
	PFIREWALL_CONTEXT_T fwCtx = (PFIREWALL_CONTEXT_T)pContext;
	struct ip *ip = NULL;
	int len = 0, off = 0;
	struct mbuf *m = NULL;
	struct icmp *icmp = NULL;
	int icmplen = 0;
	struct sctp_nets *net = NULL;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	if (fwCtx->Direction != IP_RECEIVE) {
		goto done;
	}

	if (rcvBuf0->ipr_size >= sizeof(struct ip)) {
		ip = (struct ip *)rcvBuf0->ipr_buffer;
		if (ip->ip_p != IPPROTO_ICMP) {
			goto done;
		}
	}

	for (rcvBuf = rcvBuf0; rcvBuf; rcvBuf = rcvBuf->ipr_next) {
		len += rcvBuf->ipr_size;
	}

	m = m_getm2(NULL, len, M_DONTWAIT, MT_DATA, M_PKTHDR | M_EOR);
	if (m == NULL) {
		goto done;
	}
	if ((m->m_flags & M_EOR) == 0) {
		goto done;
	}

	off = 0;
	for (rcvBuf = rcvBuf0; rcvBuf; rcvBuf = rcvBuf->ipr_next) {
		RtlCopyMemory(m->m_data + off, rcvBuf->ipr_buffer, rcvBuf->ipr_size);
		off += rcvBuf->ipr_size;
	}
	m->m_len = m->m_pkthdr.len = len;

	ip = mtod(m, struct ip *);
	switch (ip->ip_p) {
	case IPPROTO_ICMP:
		icmp_input(m, off);
		m = NULL;
		break;
	default:
		break;
	}

done:
	if (m != NULL) {
		m_freem(m);
	}
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "FirewallIcmpInput: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
#endif
	return FORWARD;
}


IPv6Action
Firewall6Input(
    IN const IPv6Addr *sourceAddress,
    IN const IPv6Addr *destinationAddress,
    IN UINT payloadLength,
    IN UCHAR headerType,
    IN const UCHAR *headerData,
    IN const void *packetContext,
    IN UINT dataLength,
    IN UINT interfaceIndex,
    IN IPv6Direction direction,
    IN BOOLEAN isLoopBack)
{
	IPv6Action ret = ActionAccept;
	struct ip6_hdr *ip6h = NULL;
	unsigned char *p;
	struct mbuf *m = NULL;
	int off = 0, plen = 0;
	struct ifnet *ifp = NULL;
	int i;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "Firewall6Input - enter\n");

	/* payloadLength includes the length of IPv6 basic header if direction == DirectionReceive */
	plen = payloadLength - sizeof(struct ip6_hdr);

	if (direction != DirectionReceive) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Firewall6Input - leave#1\n");
		goto done;
	}

	if (headerType != IPPROTO_ICMPV6 && headerType != IPPROTO_SCTP) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Firewall6Input - leave#2\n");
		goto done;
	}

	m = m_getm2(NULL, payloadLength, M_DONTWAIT, MT_DATA, M_PKTHDR | M_EOR);
	if (m == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Firewall6Input - leave#3\n");
		goto done;
	}
	if ((m->m_flags & M_EOR) == 0) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Firewall6Input - leave#4\n");
		goto done;
	}

	p = (unsigned char *)IPv6ObtainPacketData(packetContext, dataLength, 4);
	if (p == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Firewall6Input - leave#5\n");
		goto done;
	}

        IFNET_WLOCK();
        TAILQ_FOREACH(ifp, &ifnet, if_link) {
                if (ifp->if_ifIndex == interfaceIndex) {
                        break;
                }
        }
        if (ifp != NULL) {
                IFREF(ifp);
		}
        IFNET_WUNLOCK();

	ip6h = mtod(m, struct ip6_hdr *);
	RtlZeroMemory(ip6h, sizeof(struct ip6_hdr));

	ip6h->ip6_vfc = (IPV6_VERSION & IPV6_VERSION_MASK);
	ip6h->ip6_hlim = 1;
	ip6h->ip6_plen = htons(plen);
	ip6h->ip6_nxt = headerType;

	RtlCopyMemory(&ip6h->ip6_src, sourceAddress, sizeof(IPv6Addr));
	if ((IN6_IS_SCOPE_LINKLOCAL(&ip6h->ip6_src) ||
	    IN6_IS_ADDR_MC_NODELOCAL(&ip6h->ip6_src)) &&
	    ifp != NULL) {
		ip6h->ip6_src.s6_words[1] = htons(ifp->if_ifIndex & 0xffff);
	}
	RtlCopyMemory(&ip6h->ip6_dst, destinationAddress, sizeof(IPv6Addr));
	if ((IN6_IS_SCOPE_LINKLOCAL(&ip6h->ip6_dst) ||
	    IN6_IS_ADDR_MC_NODELOCAL(&ip6h->ip6_src)) &&
	    ifp != NULL) {
		ip6h->ip6_src.s6_words[1] = htons(ifp->if_ifIndex & 0xffff);
	}

	off = sizeof(struct ip6_hdr);
	RtlCopyMemory(m->m_data + off, headerData, plen - dataLength);
	off += plen - dataLength;
	RtlCopyMemory(m->m_data + off, p, dataLength);

	m->m_pkthdr.rcvif = ifp;
	m->m_len = m->m_pkthdr.len = sizeof(struct ip6_hdr) + plen;

	off = sizeof(struct ip6_hdr);
	switch (ip6h->ip6_nxt) {
	case IPPROTO_ICMPV6:
		icmp6_input(&m, &off, IPPROTO_ICMPV6);
		m = NULL;
		break;
#ifdef SCTP
	case IPPROTO_SCTP:
		sctp6_input(&m, &off, IPPROTO_SCTP);
		m = NULL;
		ret = ActionDrop;
		break;
#endif
	default:
		break;
	}
	DebugPrint(DEBUG_GENERIC_VERBOSE, "Firewall6Input - leave\n");
done:
	if (m != NULL) {
		m_freem(m);
	}
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "Firewall6Input: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
#endif
	return ret;
}


#else
void
IcmpClassify(
   IN const FWPS_INCOMING_VALUES0* inFixedValues,
   IN const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
   IN OUT void* layerData,
   IN const FWPS_FILTER0* filter,
   IN UINT64 flowContext,
   OUT FWPS_CLASSIFY_OUT0* classifyOut
   )
{
	NET_BUFFER *netBuffer = NULL;
	struct mbuf *m = NULL;
	caddr_t ptr = NULL;
	int len = 0, off = 0;
	struct ip *ip = NULL;
	struct icmp *icmp = NULL;
	int cmd = 0;
#ifdef SCTP
	struct sctphdr *sh = NULL;
	struct sockaddr_in from, to, sin;
	uint32_t vrf_id;
	struct sctp_inpcb *inp = NULL;
	struct sctp_tcb *stcb = NULL;
	struct sctp_nets *net = NULL;
#endif
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - enter\n");

	if (inFixedValues->layerId != FWPS_LAYER_INBOUND_TRANSPORT_V4 ||
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_PROTOCOL].value.uint8 != IPPROTO_ICMP ||
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_LOCAL_PORT].value.uint16 != ICMP_UNREACH) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave#1\n");
		goto done;
	}

	NdisRetreatNetBufferDataStart(NET_BUFFER_LIST_FIRST_NB((PNET_BUFFER_LIST)layerData),
	    inMetaValues->ipHeaderSize + inMetaValues->transportHeaderSize,
	    0,
	    NULL);

	len = 0;
	for (netBuffer = NET_BUFFER_LIST_FIRST_NB((PNET_BUFFER_LIST)layerData);
	     netBuffer != NULL;
	     netBuffer = NET_BUFFER_NEXT_NB(netBuffer)) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify: netBuffer=%p(DataLength=%d)\n", netBuffer, netBuffer->DataLength);
		len += netBuffer->DataLength;
	}
#ifdef SCTP
	if (len < sizeof(struct ip) + sizeof(struct icmp) + sizeof(struct sctphdr)) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave#2\n");
		goto done;
	}

	m = m_getm2(NULL, len, M_DONTWAIT, MT_DATA, M_PKTHDR | M_EOR);
	if (m == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave#3\n");
		goto done;
	}
	if ((m->m_flags & M_EOR) == 0) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave#4\n");
		goto done;
	}

	off = 0;
	for (netBuffer = NET_BUFFER_LIST_FIRST_NB((PNET_BUFFER_LIST)layerData);
	     netBuffer != NULL;
	     netBuffer = NET_BUFFER_NEXT_NB(netBuffer)) {
		ptr = NdisGetDataBuffer(netBuffer, netBuffer->DataLength, m->m_data + off, sizeof(UINT32), 0);
		if (ptr != (caddr_t)m->m_data + off) {
			RtlCopyMemory(m->m_data + off, ptr, netBuffer->DataLength);
		}
		off += netBuffer->DataLength;
	}
	m->m_len = m->m_pkthdr.len = len;

	off = 0;
	ip = (struct ip *)m->m_data;
	if (ntohs(ip->ip_len) != len) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave#5\n");
		goto done;
	}

	off += ip->ip_hl << 2;
	if (len < off + sizeof(struct icmp)) { /* make sure that longer than ICMP_MINLEN + sizeof(struct ip) */
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave#5\n");
		goto done;
	}
	icmp = (struct icmp *)(m->m_data + off);

	off += ICMP_MINLEN + (icmp->icmp_ip.ip_hl << 2);
	if (len < off + sizeof(struct sctphdr) ||
	    ntohs(icmp->icmp_ip.ip_len) - (icmp->icmp_ip.ip_hl << 2) < sizeof(struct sctphdr) ||
	    icmp->icmp_ip.ip_p != IPPROTO_SCTP) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave#6\n");
		goto done;
	}
	sh = (struct sctphdr *)(m->m_data + off);

	RtlZeroMemory(&from, sizeof(from));
	RtlZeroMemory(&to, sizeof(to));

	to.sin_family = AF_INET;
	to.sin_port = sh->src_port;
	to.sin_addr.s_addr = ntohl(
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_LOCAL_ADDRESS].value.uint32);

	from.sin_family = AF_INET;
	from.sin_port = sh->dest_port;
	from.sin_addr.s_addr = ntohl(
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_REMOTE_ADDRESS].value.uint32);

	vrf_id = SCTP_DEFAULT_VRFID;

#if 0
	SCTPDBG_ADDR(SCTP_DEBUG_NOISY, (struct sockaddr *)&to);
	SCTPDBG_ADDR(SCTP_DEBUG_NOISY, (struct sockaddr *)&from);
#endif
	stcb = sctp_findassociation_addr_sa((struct sockaddr *)&to,
	    (struct sockaddr *)&from, &inp, &net, 1, vrf_id);
	DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify: stcb=%p,code=%d\n",
	    stcb, inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_REMOTE_PORT].value.uint16);
	if (stcb != NULL) {
		SCTP_TCB_UNLOCK(stcb);
	} else if (
	    inp != NULL) {
		SCTP_INP_WLOCK(inp);
		SCTP_INP_DECR_REF(inp);
		SCTP_INP_WUNLOCK(inp);
	}
#endif

	switch (inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_REMOTE_PORT].value.uint16) {
	case ICMP_UNREACH_NET:
	case ICMP_UNREACH_HOST:
	case ICMP_UNREACH_SRCFAIL:
	case ICMP_UNREACH_NET_UNKNOWN:
	case ICMP_UNREACH_HOST_UNKNOWN:
	case ICMP_UNREACH_ISOLATED:
	case ICMP_UNREACH_PRECEDENCE_CUTOFF:
		cmd = PRC_UNREACH_NET;
		break;

	case ICMP_UNREACH_NEEDFRAG:
		cmd = PRC_MSGSIZE;
		break;

	case ICMP_UNREACH_PROTOCOL:
	case ICMP_UNREACH_PORT:
		if (stcb != NULL) {
			cmd = PRC_UNREACH_PORT;
		} else { /* From router, invalid response */
			cmd = PRC_UNREACH_NET;
			icmp->icmp_code = ICMP_UNREACH_NET; /* XXX */
		}
		break;

	case ICMP_UNREACH_NET_PROHIB:
	case ICMP_UNREACH_HOST_PROHIB:
	case ICMP_UNREACH_FILTER_PROHIB:
		cmd = PRC_UNREACH_ADMIN_PROHIB;
		break;

	default:
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave#7\n");
		goto done;
	}

	RtlZeroMemory(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr = icmp->icmp_ip.ip_dst;

	switch (icmp->icmp_ip.ip_p) {
#ifdef SCTP
	case IPPROTO_SCTP:
		sctp_ctlinput(cmd, (struct sockaddr *)&sin, &icmp->icmp_ip);
		break;
#endif
	default:
		break;
	}
	DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave\n");
done:
	if (m != NULL) {
		m_freem(m);
	}
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "IcmpClassify: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "IcmpClassify: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return;
}

void
Icmp6Classify(
   IN const FWPS_INCOMING_VALUES0* inFixedValues,
   IN const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
   IN OUT void* layerData,
   IN const FWPS_FILTER0* filter,
   IN UINT64 flowContext,
   OUT FWPS_CLASSIFY_OUT0* classifyOut
   )
{
	NET_BUFFER *netBuffer = NULL;
	struct mbuf *m = NULL;
	caddr_t ptr = NULL;
	int len = 0, off = 0;
	struct ip6_hdr *ip6 = NULL;
	struct icmp6_hdr *icmp6 = NULL;
	struct ip6ctlparam ip6cp;
	int cmd = 0;
#ifdef SCTP
	struct sockaddr_in6 from, to;
	struct sctphdr *sh = NULL;
	uint32_t vrf_id;
	struct sctp_inpcb *inp = NULL;
	struct sctp_tcb *stcb = NULL;
	struct sctp_nets *net = NULL;
#endif
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify - enter\n");

	if (inFixedValues->layerId != FWPS_LAYER_INBOUND_TRANSPORT_V6 ||
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V6_IP_PROTOCOL].value.uint8 != IPPROTO_ICMPV6 ||
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V6_IP_LOCAL_PORT].value.uint16 != ICMP6_DST_UNREACH) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify - leave#1\n");
		goto done;
	}

	NdisRetreatNetBufferDataStart(NET_BUFFER_LIST_FIRST_NB((PNET_BUFFER_LIST)layerData),
	    inMetaValues->ipHeaderSize + inMetaValues->transportHeaderSize,
	    0,
	    NULL);

	len = 0;
	for (netBuffer = NET_BUFFER_LIST_FIRST_NB((PNET_BUFFER_LIST)layerData);
	     netBuffer != NULL;
	     netBuffer = NET_BUFFER_NEXT_NB(netBuffer)) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify: netBuffer=%p(DataLength=%d)\n", netBuffer, netBuffer->DataLength);
		len += netBuffer->DataLength;
	}
	DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify: len=%d\n", len);
	if (len < sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr) + sizeof(struct sctphdr)) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify - leave#2\n");
		goto done;
	}

	m = m_getm2(NULL, len, M_DONTWAIT, MT_DATA, M_PKTHDR | M_EOR);
	if (m == NULL) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify - leave#3\n");
		goto done;
	}
	if ((m->m_flags & M_EOR) == 0) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify - leave#4\n");
		goto done;
	}

	off = 0;
	for (netBuffer = NET_BUFFER_LIST_FIRST_NB((PNET_BUFFER_LIST)layerData);
	     netBuffer != NULL;
	     netBuffer = NET_BUFFER_NEXT_NB(netBuffer)) {
		ptr = NdisGetDataBuffer(netBuffer, netBuffer->DataLength, m->m_data + off, sizeof(UINT32), 0);
		if (ptr != (caddr_t)m->m_data + off) {
			RtlCopyMemory(m->m_data + off, ptr, netBuffer->DataLength);
		}
		off += netBuffer->DataLength;
	}
	m->m_len = m->m_pkthdr.len = len;

	off = 0;
	ip6 = (struct ip6_hdr *)m->m_data;

	off += sizeof(struct ip6_hdr);
	if (len < off + sizeof(struct icmp6_hdr)) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify - leave#4\n");
		goto done;
	}
	icmp6 = (struct icmp6_hdr *)(m->m_data + off);

	off += sizeof(struct icmp6_hdr);
	if (len < off + sizeof(struct sctphdr) ||
	    ntohs(icmp6->icmp6_ip6.ip6_plen) < sizeof(struct sctphdr) ||
	    icmp6->icmp6_ip6.ip6_nxt != IPPROTO_SCTP) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpClassify - leave#5\n");
		goto done;
	}
	sh = (struct sctphdr *)((caddr_t)ip6 + off);

	to.sin6_family = AF_INET6;
	to.sin6_port = sh->src_port;
	RtlCopyMemory(&to.sin6_addr,
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V6_IP_LOCAL_ADDRESS].value.byteArray16,
	    sizeof(FWP_BYTE_ARRAY16));
	to.sin6_scope_id =
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V6_INTERFACE_INDEX].value.uint32;

	from.sin6_family = AF_INET;
	from.sin6_port = sh->dest_port;
	RtlCopyMemory(&from.sin6_addr,
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V6_IP_REMOTE_ADDRESS].value.byteArray16,
	    sizeof(FWP_BYTE_ARRAY16));
	from.sin6_scope_id =
	    inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V6_INTERFACE_INDEX].value.uint32;

	vrf_id = SCTP_DEFAULT_VRFID;

#if 0
	SCTPDBG_ADDR(SCTP_DEBUG_NOISY, (struct sockaddr *)&to);
	SCTPDBG_ADDR(SCTP_DEBUG_NOISY, (struct sockaddr *)&from);
#endif
	stcb = sctp_findassociation_addr_sa((struct sockaddr *)&to,
	    (struct sockaddr *)&from, &inp, &net, 1, vrf_id);
	if (stcb != NULL) {
		SCTP_TCB_UNLOCK(stcb);
	} else if (
	    inp != NULL) {
		SCTP_INP_WLOCK(inp);
		SCTP_INP_DECR_REF(inp);
		SCTP_INP_WUNLOCK(inp);
	}

	switch (inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V6_IP_REMOTE_PORT].value.uint16) {
	case ICMP6_DST_UNREACH_NOROUTE:
		cmd = PRC_UNREACH_NET;
		break;
	case ICMP6_DST_UNREACH_ADMIN:
		cmd = PRC_UNREACH_ADMIN_PROHIB;
		break;
	case ICMP6_DST_UNREACH_BEYONDSCOPE:
		cmd = PRC_PARAMPROB;
		break;
	case ICMP6_DST_UNREACH_ADDR:
		cmd = PRC_HOSTDEAD;
		break;
	case ICMP6_DST_UNREACH_NOPORT:
		if (stcb != NULL) {
			cmd = PRC_UNREACH_PORT;
		} else {
			cmd = PRC_HOSTDEAD;
		}
		break;
	default:
		DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify - leave#6\n");
		goto done;
	}

	RtlZeroMemory(&ip6cp, sizeof(ip6cp));
	ip6cp.ip6c_m = m;
	ip6cp.ip6c_icmp6 = icmp6;
	ip6cp.ip6c_ip6 = &icmp6->icmp6_ip6;
	ip6cp.ip6c_off = off;
	ip6cp.ip6c_dst = &to;
	ip6cp.ip6c_finaldst = &to.sin6_addr;
	ip6cp.ip6c_src = &from;
	ip6cp.ip6c_nxt = icmp6->icmp6_ip6.ip6_nxt;

	switch (icmp6->icmp6_ip6.ip6_nxt) {
#ifdef SCTP
	case IPPROTO_SCTP:
		sctp6_ctlinput(cmd, (struct sockaddr *)&to, &ip6cp);
		break;
#endif
	default:
		break;
	}
	DebugPrint(DEBUG_GENERIC_VERBOSE, "Icmp6Classify - leave\n");
done:
	if (m != NULL) {
		m_freem(m);
	}
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "Icmp6Classify: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		DbgBreakPoint();
	}
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL && RaiseDpcLevel[KeGetCurrentProcessorNumber()] != 0) {
		DebugPrint(DEBUG_LOCK_ERROR, "Icmp6Classify: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		DbgBreakPoint();
	}
#endif
	return;
}

NTSTATUS
IcmpNotify(
   IN FWPS_CALLOUT_NOTIFY_TYPE notifyType,
   IN const GUID* filterKey,
   IN const FWPS_FILTER0* filter
   )
{
	DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpNotify - enter\n");
	DebugPrint(DEBUG_GENERIC_VERBOSE, "IcmpNotify - leave\n");
	return STATUS_SUCCESS;
}
#endif

#ifdef SCTP
void
sctp_over_udp_restart(void)
{
	DebugPrint(DEBUG_GENERIC_VERBOSE, "sctp_over_udp_restart - enter\n");
	KeSetEvent(&ReloadThreadEvents[1], IO_NO_INCREMENT, FALSE);
	DebugPrint(DEBUG_GENERIC_VERBOSE, "sctp_over_udp_restart - leave\n");
}
#endif

VOID
NTAPI
AioCsqRemoveIrp(
    IN PIO_CSQ unusedCsq,
    IN PIRP irp)
{
	DebugPrint(DEBUG_GENERIC_VERBOSE, "SbCsqRemoveIrp - enter\n");
	RemoveEntryList(&irp->Tail.Overlay.ListEntry);
	DebugPrint(DEBUG_GENERIC_VERBOSE, "SbCsqRemoveIrp - leave\n");
}

PIRP
NTAPI
AioCsqPeekNextIrp(
    IN PIO_CSQ csq,
    IN PIRP irp,
    IN PVOID peekContext)
{
	struct sockbufqueue *queue;
	PIRP nextIrp = NULL;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqPeekNextIrp - enter\n");
	queue = (struct sockbufqueue *)csq;
	if (irp != NULL) {
		nextIrp = CONTAINING_RECORD(&irp->Tail.Overlay.ListEntry.Flink, IRP, Tail.Overlay.ListEntry);
		DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqPeekNextIrp - leave#1\n");
		return nextIrp;
	}

	if (IsListEmpty(&queue->irpList)) {
		DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqPeekNextIrp - leave#2\n");
		return NULL;
	}

	nextIrp = CONTAINING_RECORD(queue->irpList.Flink, IRP, Tail.Overlay.ListEntry);
	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqPeekNextIrp - leave\n");
	return nextIrp;
}

VOID
NTAPI
AioCsqAcquireLock(
    IN PIO_CSQ csq,
    IN PKIRQL irql)
{
	struct sockbufqueue *queue = (struct sockbufqueue *)csq;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqAcquireLock - enter\n");
	spinlock_acquire(&queue->lock);
	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqAcquireLock - leave\n");
}

VOID
NTAPI
AioCsqReleaseLock(
    IN PIO_CSQ csq,
    IN KIRQL irql)
{
	struct sockbufqueue *queue = (struct sockbufqueue *)csq;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqReleaseLock - enter\n");
	spinlock_release(&queue->lock);
	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqReleaseLock - leave\n");
}

VOID
NTAPI
AioCsqCompleteCanceledIrp(
    IN PIO_CSQ unusedCsq,
    IN PIRP irp)
{
	PIO_STACK_LOCATION irpSp = NULL;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqCompleteCanceledIrp - enter\n");

	irpSp = IoGetCurrentIrpStackLocation(irp);
	if (irpSp->DeviceObject == SctpSocketDeviceObject) {
		switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_SOCKET_SEND:
		case IOCTL_SOCKET_SENDTO:
			SCTPDispatchSendRequestCanceled(irp, irpSp);
			break;
		case IOCTL_SOCKET_SENDMSG:
			SCTPDispatchSendMsgRequestCanceled(irp, irpSp);
			break;
		case IOCTL_SOCKET_RECV:
		case IOCTL_SOCKET_RECVFROM:
			SCTPDispatchRecvRequestCanceled(irp, irpSp);
			break;
		case IOCTL_SOCKET_RECVMSG:
			SCTPDispatchRecvMsgRequestCanceled(irp, irpSp);
			break;
		default:
			break;
		}
	}
	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_CANCELLED;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqCompleteCanceledIrp - leave\n");
}


VOID
NTAPI
AioCsqInsertIrp(
    IN PIO_CSQ csq,
    IN PIRP irp)
{
	struct sockbufqueue *queue;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqInsertIrp - enter\n");
	queue = (struct sockbufqueue *)csq;
	InsertTailList(&queue->irpList, &irp->Tail.Overlay.ListEntry);
	DebugPrint(DEBUG_GENERIC_VERBOSE, "AioCsqInsertIrp - leave\n");
}

VOID
aio_swake_cb(
    IN struct socket *so,
    IN struct sockbuf *sb)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIRP irp = NULL;
	PIO_STACK_LOCATION irpSp = NULL;
	PKPROCESS process = NULL;
	KAPC_STATE apcState;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "aio_swake - enter\n");

	while ((irp = IoCsqRemoveNextIrp((PIO_CSQ)&sb->sb_csq, NULL)) != NULL) {
		irpSp = IoGetCurrentIrpStackLocation(irp);
		irp->IoStatus.Information = 0;

		if (irpSp->DeviceObject == SctpSocketDeviceObject) {
			switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
			case IOCTL_SOCKET_SEND:
			case IOCTL_SOCKET_SENDTO:
				status = SCTPDispatchSendRequestDeferred(irp, irpSp);
				break;
			case IOCTL_SOCKET_SENDMSG:
				status = SCTPDispatchSendMsgRequestDeferred(irp, irpSp);
				break;
			case IOCTL_SOCKET_RECV:
			case IOCTL_SOCKET_RECVFROM:
				status = SCTPDispatchRecvRequestDeferred(irp, irpSp);
				break;
			case IOCTL_SOCKET_RECVMSG:
				status = SCTPDispatchRecvMsgRequestDeferred(irp, irpSp);
				break;
			default:
				status = STATUS_INVALID_DEVICE_REQUEST;
			}
#ifdef SCTP
		} else if (
		    irpSp->DeviceObject == SctpTdiTcpDeviceObject) {
			status = STATUS_INVALID_DEVICE_REQUEST;
		} else if (
		    irpSp->DeviceObject == SctpTdiUdpDeviceObject) {
			status = STATUS_INVALID_DEVICE_REQUEST;
#endif
		} else {
			status = STATUS_INVALID_DEVICE_REQUEST;
		}

		if (status == STATUS_PENDING) {
			DebugPrint(DEBUG_GENERIC_VERBOSE, "aio_swake - leave#2\n");
			return;
		}
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
	}

	sb->sb_flags &= ~SB_AIO;
	DebugPrint(DEBUG_GENERIC_VERBOSE, "aio_swake - leave\n");
}


NTSTATUS
SCTPCreate(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp)
{
	PIO_STACK_LOCATION irpSp;
	NTSTATUS status = STATUS_SUCCESS;
	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPCreate - enter\n");

	irpSp = IoGetCurrentIrpStackLocation(irp);

	if (deviceObject == SctpSocketDeviceObject) {
		status = SCTPCreateSocket(irp, irpSp);
	} else if (
		deviceObject == SctpDeviceObject) {
		status = STATUS_SUCCESS;
#ifdef SCTP
	} else if (
		deviceObject == SctpTdiTcpDeviceObject) {
		status = SCTPCreateTdi(irp, irpSp, SOCK_STREAM, IPPROTO_SCTP);
	} else if (
		deviceObject == SctpTdiUdpDeviceObject) {
		status = SCTPCreateTdi(irp, irpSp, SOCK_SEQPACKET, IPPROTO_SCTP);
#endif
	} else {
		status = STATUS_INVALID_EA_NAME;
	}

	irp->IoStatus.Status = status;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NETWORK_INCREMENT);

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPCreate - leave\n");
	return status;
}

NTSTATUS
SCTPDispatch(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp)
{
	NTSTATUS status;
	PIO_STACK_LOCATION irpSp;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPDispatch - enter\n");
	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPDispatch - leave\n");
	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS
SCTPDispatchDeviceControl(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp)
{
	PIO_STACK_LOCATION irpSp;
	NTSTATUS status = STATUS_SUCCESS;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPDispatchDeviceControl - enter\n");

	irp->IoStatus.Information = 0;
	irpSp = IoGetCurrentIrpStackLocation(irp);

	if (deviceObject == SctpSocketDeviceObject) {
		switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_SOCKET_OPEN:
			status = SCTPDispatchOpenRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_BIND:
			status = SCTPDispatchBindRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_CONNECT:
			status = SCTPDispatchConnectRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_LISTEN:
			status = SCTPDispatchListenRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_ACCEPT:
			status = SCTPDispatchAcceptRequest(irp, irpSp);
			break;
#ifdef SCTP
		case IOCTL_SOCKET_PEELOFF:
			status = SCTPDispatchPeeloffRequest(irp, irpSp);
			break;
#endif
		case IOCTL_SOCKET_SEND:
		case IOCTL_SOCKET_SENDTO:
			status = SCTPDispatchSendRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_SENDMSG:
			status = SCTPDispatchSendMsgRequest(irp, irpSp);
			break;
#ifdef SCTP
		case IOCTL_SOCKET_SCTPSEND:
			status = SCTPDispatchSctpSendRequest(irp, irpSp);
			break;
#endif
		case IOCTL_SOCKET_RECV:
		case IOCTL_SOCKET_RECVFROM:
			status = SCTPDispatchRecvRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_RECVMSG:
			status = SCTPDispatchRecvMsgRequest(irp, irpSp);
			break;
#ifdef SCTP
		case IOCTL_SOCKET_SCTPRECV:
			status = SCTPDispatchSctpRecvRequest(irp, irpSp);
			break;
#endif
		case IOCTL_SOCKET_SELECT:
			status = SCTPDispatchSelectRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_EVENTSELECT:
			status = SCTPDispatchEventSelectRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_ENUMNETWORKEVENTS:
			status = SCTPDispatchEnumNetworkEventsRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_SETSOCKOPT:
			status = SCTPDispatchSetOptionRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_GETSOCKOPT:
			status = SCTPDispatchGetOptionRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_GETSOCKNAME:
			status = SCTPDispatchGetSockName(irp, irpSp);
			break;
		case IOCTL_SOCKET_GETPEERNAME:
			status = SCTPDispatchGetPeerName(irp, irpSp);
			break;
		case IOCTL_SOCKET_SHUTDOWN:
			status = SCTPDispatchShutdown(irp, irpSp);
			break;
		case IOCTL_SOCKET_IOCTL:
			status = SCTPDispatchIoctl(irp, irpSp);
			break;
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}
#ifdef SCTP
	} else if (
	    deviceObject == SctpTdiTcpDeviceObject ||
	    deviceObject == SctpTdiUdpDeviceObject) {
		switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_SOCKET_SETSOCKOPT:
			status = SCTPDispatchSetOptionRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_GETSOCKOPT:
			status = SCTPDispatchGetOptionRequest(irp, irpSp);
			break;
		case IOCTL_SOCKET_GETSOCKNAME:
			status = SCTPDispatchGetSockName(irp, irpSp);
			break;
		case IOCTL_SOCKET_GETPEERNAME:
			status = SCTPDispatchGetPeerName(irp, irpSp);
			break;
		default:
			status = TdiMapUserRequest(deviceObject, irp, irpSp);
			if (NT_SUCCESS(status)) {
				status = SCTPDispatchInternalDeviceControl(deviceObject, irp);

				DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPDispatchDeviceControl - leave#1\n");
                		return status;
			}
			break;
		}
#endif
	} else {
		switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_SCTP_SYSCTL:
			status = SCTPDispatchSysctl(irp, irpSp);
			break;
		default:
			status = STATUS_INVALID_PARAMETER;
		}
	}
	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPDispatchDeviceControl - leave\n");
	if (status != STATUS_PENDING) {
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NETWORK_INCREMENT);
	}

#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "SCTPDispatchDeviceControl: irql=%u,oldirql=%u,code=0x%x\n", KeGetCurrentIrql(), oldIrql, irpSp->Parameters.DeviceIoControl.IoControlCode);
		if (KeGetCurrentIrql() >= DISPATCH_LEVEL) {
			DebugPrint(DEBUG_LOCK_ERROR, "SCTPDispatchDeviceControl: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		}
		DbgBreakPoint();
	}
#endif
	return status;
}

NTSTATUS
SCTPDispatchInternalDeviceControl(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp)
{
	PIO_STACK_LOCATION irpSp;
	NTSTATUS status = STATUS_SUCCESS;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPDispatchInternalDeviceControl - enter\n");

	irp->IoStatus.Information = 0;
	irpSp = IoGetCurrentIrpStackLocation(irp);

#ifdef SCTP
	if (deviceObject != SctpTdiTcpDeviceObject &&
	    deviceObject != SctpTdiUdpDeviceObject) {
		status = STATUS_NOT_IMPLEMENTED;
		DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPDispatchInternalDeviceControl - leave#1\n");
		goto done;
	}
#else
	status = STATUS_NOT_IMPLEMENTED;
	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPDispatchInternalDeviceControl - leave#1\n");
	goto done;
#endif

	if (PtrToLong(irpSp->FileObject->FsContext2) == TDI_CONNECTION_FILE) {
		/* One-to-One */
		switch (irpSp->MinorFunction) {
		case TDI_ASSOCIATE_ADDRESS:
			status = SCTPDispatchTdiAssociateAddress(irp, irpSp);
			break;
		case TDI_DISASSOCIATE_ADDRESS:
			status = SCTPDispatchTdiDisassociateAddress(irp, irpSp);
			break;
		case TDI_LISTEN:
		case TDI_ACCEPT:
			status = STATUS_NOT_IMPLEMENTED;
			break;
		case TDI_CONNECT:
			status = SCTPDispatchTdiConnect(irp, irpSp);
			break;
		case TDI_DISCONNECT:
			status = SCTPDispatchTdiDisconnect(irp, irpSp);
			break;
		case TDI_RECEIVE:
			status = SCTPDispatchTdiReceive(irp, irpSp);
			break;
		case TDI_SEND:
			status = SCTPDispatchTdiSend(irp, irpSp);
			break;
		case TDI_QUERY_INFORMATION:
			status = SCTPDispatchTdiQueryInformation(irp, irpSp);
			break;
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}
	} else if (
	    PtrToLong(irpSp->FileObject->FsContext2) == TDI_TRANSPORT_ADDRESS_FILE) {
		/* One-to-Many */
		switch (irpSp->MinorFunction) {
		case TDI_RECEIVE_DATAGRAM:
			status = SCTPDispatchTdiReceiveDatagram(irp, irpSp);
			break;
		case TDI_SEND_DATAGRAM:
			status = SCTPDispatchTdiSendDatagram(irp, irpSp);
			break;
		case TDI_SET_EVENT_HANDLER:
			status = SCTPDispatchTdiSetEventHandler(irp, irpSp);
			break;
		case TDI_QUERY_INFORMATION:
			status = SCTPDispatchTdiQueryInformation(irp, irpSp);
			break;
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}
	} else {
		switch (irpSp->MinorFunction) {
		case TDI_QUERY_INFORMATION:
			status = SCTPDispatchTdiQueryInformation(irp, irpSp);
			break;
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}
	}

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPDispatchInternalDeviceControl - leave\n");
done:
	irp->IoStatus.Status = status;
	if (status != STATUS_PENDING) {
		IoCompleteRequest(irp, IO_NETWORK_INCREMENT);
	}
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "SCTPDispatchInternalDeviceControl: irql=%u,oldirql=%u,code=0x%x\n", KeGetCurrentIrql(), oldIrql, irpSp->Parameters.DeviceIoControl.IoControlCode);
		if (KeGetCurrentIrql() >= DISPATCH_LEVEL) {
			DebugPrint(DEBUG_LOCK_ERROR, "SCTPDispatchInternalDeviceControl: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		}
		DbgBreakPoint();
	}
#endif
	return status;
}

NTSTATUS
SCTPCleanup(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp)
{
	NTSTATUS status;

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPCleanup - enter\n");

	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NETWORK_INCREMENT);

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPCleanup - leave\n");
	return STATUS_SUCCESS;
}

NTSTATUS
SCTPClose(
    IN PDEVICE_OBJECT deviceObject,
    IN PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp = NULL;
#ifdef DBG
	KIRQL oldIrql = KeGetCurrentIrql();
#endif

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPClose - enter\n");
	irpSp = IoGetCurrentIrpStackLocation(irp);

	if (deviceObject == SctpSocketDeviceObject) {
		status = SCTPCloseSocket(irp, irpSp);
#ifdef SCTP
	} else if (
	    deviceObject == SctpTdiTcpDeviceObject || deviceObject == SctpTdiUdpDeviceObject
	    ) {
		status = SCTPCloseTdi(irp, irpSp);
#endif
	} else {
		status = STATUS_INVALID_DEVICE_REQUEST;
	}
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NETWORK_INCREMENT);

	DebugPrint(DEBUG_GENERIC_VERBOSE, "SCTPClose - leave\n");
#ifdef DBG
	if (KeGetCurrentIrql() != oldIrql) {
		DebugPrint(DEBUG_LOCK_ERROR, "SCTPClose: irql=%u,oldirql=%u\n", KeGetCurrentIrql(), oldIrql);
		if (KeGetCurrentIrql() >= DISPATCH_LEVEL) {
			DebugPrint(DEBUG_LOCK_ERROR, "SCTPClose: RaiseDpcLevel[%d]=%d\n", KeGetCurrentProcessorNumber(), RaiseDpcLevel[KeGetCurrentProcessorNumber()]);
		}
		DbgBreakPoint();
	}
#endif
	return status;
}
