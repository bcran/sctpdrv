/*
 * Copyright (c) 2008 CO-CONV, Corp.
 * Copyright (C) 2010 Bruce Cran.
 * All rights reserved.
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
#ifndef _SYS_SYSCALL_H_
#define _SYS_SYSCALL_H_

#ifndef CTL_CODE
#include <devioctl.h>
#endif

#pragma warning(push)
#pragma warning(disable: 4200)

#define SCTP_ONE_TO_ONE_DEVICE_NAME  L"SctpTcp"
#define SCTP_ONE_TO_MANY_DEVICE_NAME L"SctpUdp"
#define SCTP_DEVICE_NAME             L"Sctp"
#define SCTP_SOCKET_DEVICE_NAME      L"SctpSocket"
#define DD_DEVICE_NAMESPACE	         L"\\Device\\"
#define WIN_DEVICE_NAMESPACE         L"\\\\.\\"

#define DD_SCTP_ONE_TO_ONE_DEVICE_NAME  DD_DEVICE_NAMESPACE SCTP_ONE_TO_ONE_DEVICE_NAME
#define DD_SCTP_ONE_TO_MANY_DEVICE_NAME DD_DEVICE_NAMESPACE SCTP_ONE_TO_MANY_DEVICE_NAME
#define DD_SCTP_DEVICE_NAME             DD_DEVICE_NAMESPACE SCTP_DEVICE_NAME
#define DD_SCTP_SOCKET_DEVICE_NAME      DD_DEVICE_NAMESPACE SCTP_SOCKET_DEVICE_NAME
#define WIN_SCTP_DEVICE_NAME            WIN_DEVICE_NAMESPACE SCTP_DEVICE_NAME
#define WIN_SCTP_SOCKET_DEVICE_NAME     WIN_DEVICE_NAMESPACE SCTP_SOCKET_DEVICE_NAME


#define FSCTL_TCP_BASE	FILE_DEVICE_NETWORK

#define _TCP_CTL_CODE(function, method, access) \
	CTL_CODE(FSCTL_TCP_BASE, function, method, access)

#define IOCTL_TCP_QUERY_INFORMATION_EX  \
	_TCP_CTL_CODE(0, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_TCP_SET_INFORMATION_EX  \
	_TCP_CTL_CODE(1, METHOD_BUFFERED, FILE_WRITE_DATA)

#define FSCTL_SCTP_BASE	FILE_DEVICE_NETWORK

#define _SCTP_CTL_CODE(function, method, access) \
	CTL_CODE(FSCTL_SCTP_BASE, function, method, access)

#define IOCTL_SCTP_SYSCTL \
	_SCTP_CTL_CODE(0, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define SOCKET_FD_READ_BIT	0
#define SOCKET_FD_READ		(1 << FD_READ_BIT)
#define SOCKET_FD_WRITE_BIT	1
#define SOCKET_FD_WRITE         (1 << FD_WRITE_BIT)
#define SOCKET_FD_OOB_BIT	2
#define SOCKET_FD_OOB		(1 << FD_OOB_BIT)
#define SOCKET_FD_ACCEPT_BIT	3
#define SOCKET_FD_ACCEPT	(1 << FD_ACCEPT_BIT)
#define SOCKET_FD_CONNECT_BIT	4
#define SOCKET_FD_CONNECT	(1 << FD_CONNECT_BIT)
#define SOCKET_FD_CLOSE_BIT	5
#define SOCKET_FD_CLOSE		(1 << FD_CLOSE_BIT)
#define SOCKET_FD_QOS_BIT       6
#define SOCKET_FD_QOS		(1 << FD_QOS_BIT)
#define SOCKET_FD_GROUP_QOS_BIT	7
#define SOCKET_FD_GROUP_QOS	(1 << FD_GROUP_QOS_BIT)
#define SOCKET_FD_ROUTING_INTERFACE_CHANGE_BIT 8
#define SOCKET_FD_ROUTING_INTERFACE_CHANGE (1 << FD_ROUTING_INTERFACE_CHANGE_BIT)
#define SOCKET_FD_ADDRESS_LIST_CHANGE_BIT 9
#define SOCKET_FD_ADDRESS_LIST_CHANGE (1 << FD_ADDRESS_LIST_CHANGE_BIT)
#define SOCKET_FD_MAX_EVENTS    10
#define SOCKET_FD_ALL_EVENTS	((1 << FD_MAX_EVENTS) - 1)

#define SOCKET_IOCPARM_MASK	0x7f
#define SOCKET_IOC_VOID		0x20000000
#define SOCKET_IOC_OUT		0x40000000
#define SOCKET_IOC_IN		0x80000000
#define SOCKET_IOC_INOUT	(SOCKET_IOC_IN | SOCKET_IOC_OUT)
#define SOCKET_IO(x, y)		(SOCKET_IOC_VOID | ((x) << 8) | (y))
#define SOCKET_IOR(x, y, t)	(SOCKET_IOC_OUT | (((long)sizeof(t) & SOCKET_IOCPARM_MASK) << 16) | ((x) << 8) | (y))
#define SOCKET_IOW(x, y, t)	(SOCKET_IOC_IN | (((long)sizeof(t) & SOCKET_IOCPARM_MASK) << 16)| ((x) << 8) | (y))

#define SOCKET_FIONBIO		SOCKET_IOW('f', 126, u_long)

#define SOCKET_FD_SETSIZE 64

typedef struct _SOCKET_WSABUF {
	UINT	len;
	PCHAR	buf;
} SOCKET_WSABUF, *PSOCKET_WSABUF;

typedef struct _SOCKET_WSABUF32 {
	ULONG len;
	char*POINTER_32 buf;
} SOCKET_WSABUF32, *PSOCKET_WSABUF32;

typedef struct _SOCKET_WSAMSG {
	LPSOCKADDR name;
	INT	namelen;
	PSOCKET_WSABUF lpBuffers;
	DWORD	dwBufferCount;
	SOCKET_WSABUF	Control;
	DWORD	dwFlags;
} SOCKET_WSAMSG, *PSOCKET_WSAMSG;

typedef struct _SOCKET_WSAMSG32 {
	SOCKADDR *POINTER_32	name;
	INT						namelen;
	SOCKET_WSABUF32 *POINTER_32	lpBuffers;

#if(_WIN32_WINNT >= 0x0600)
	ULONG	dwBufferCount;
#else
	DWORD	dwBufferCount;
#endif //(_WIN32_WINNT>=0x0600)

	SOCKET_WSABUF32	Control;

#if(_WIN32_WINNT >= 0x0600)
	ULONG		dwFlags;
#else
	DWORD		dwFlags;
#endif //(_WIN32_WINNT>=0x0600)
} SOCKET_WSAMSG32, *PSOCKET_WSAMSG32;

typedef struct _SOCKET_FD_SET {
	UINT	fd_count;
	HANDLE	fd_array[0];
} SOCKET_FD_SET, *PSOCKET_FD_SET;

typedef struct fd_set32 {
        UINT fd_count;               /* how many are SET? */
        ULONG fd_array[SOCKET_FD_SETSIZE];   /* an array of SOCKETs */
} fd_set32;

typedef fd_set32 SOCKET_FD_SET32;
typedef fd_set32 *PSOCKET_FD_SET32;

typedef struct _SOCKET_WSANETWORKEVENTS {
	long lNetworkEvents;
	int iErrorCode[SOCKET_FD_MAX_EVENTS];
} SOCKET_WSANETWORKEVENTS,  *PSOCKET_WSANETWORKEVENTS;

typedef struct _SOCKET_TIMEVAL {
	long	tv_sec;
	long	tv_usec;
} SOCKET_TIMEVAL, *PSOCKET_TIMEVAL;

typedef struct _SOCKET_OPEN_REQUEST {
	int	af;
	int	type;
	int	protocol;
} SOCKET_OPEN_REQUEST , *PSOCKET_OPEN_REQUEST;

typedef struct _SOCKET_ACCEPT_REQUEST {
	struct sockaddr	*addr;
	int		addrlen;
} SOCKET_ACCEPT_REQUEST, *PSOCKET_ACCEPT_REQUEST;

typedef struct _SOCKET_OPEN_REQUEST32 {
	struct sockaddr*POINTER_32	addr;
	int							addrlen;
} SOCKET_ACCEPT_REQUEST32, *PSOCKET_ACCEPT_REQUEST32;

typedef struct _SOCKET_SEND_REQUEST {
	PSOCKET_WSABUF	 lpBuffers;
	DWORD			 dwBufferCount;
	struct sockaddr	*lpTo;
	int				 iTolen;
	DWORD			 dwFlags;
} SOCKET_SEND_REQUEST, *PSOCKET_SEND_REQUEST;

typedef struct _SOCKET_SEND_REQUEST32 {
	SOCKET_WSABUF32*POINTER_32	lpBuffers;
	DWORD					dwBufferCount;
	struct sockaddr*POINTER_32	lpTo;
	int					iTolen;
	DWORD				dwFlags;
} SOCKET_SEND_REQUEST32, *PSOCKET_SEND_REQUEST32;

typedef struct _SOCKET_SENDMSG_REQUEST {
	PSOCKET_WSAMSG		lpMsg;
	DWORD				dwFlags;
} SOCKET_SENDMSG_REQUEST, *PSOCKET_SENDMSG_REQUEST;

typedef struct _SOCKET_SENDMSG_REQUEST32 {
	SOCKET_WSAMSG32*POINTER_32	lpMsg;
	DWORD						dwFlags;
} SOCKET_SENDMSG_REQUEST32, *PSOCKET_SENDMSG_REQUEST32;

typedef struct _SOCKET_RECV_REQUEST {
	PSOCKET_WSABUF	 lpBuffers;
	DWORD			 dwBufferCount;
	struct sockaddr	*lpFrom;
	int				*lpFromlen;
	DWORD			*lpFlags;
} SOCKET_RECV_REQUEST, *PSOCKET_RECV_REQUEST;

typedef struct _SOCKET_RECV_REQUEST32 {
	SOCKET_WSABUF32*POINTER_32		lpBuffers;
	DWORD							dwBufferCount;
	struct sockaddr*POINTER_32		lpFrom;
	int*POINTER_32					lpFromlen;
	DWORD*POINTER_32				lpFlags;
} SOCKET_RECV_REQUEST32, *PSOCKET_RECV_REQUEST32;

typedef struct _SOCKET_RECVMSG_REQUEST {
	PSOCKET_WSAMSG	lpMsg;
} SOCKET_RECVMSG_REQUEST, *PSOCKET_RECVMSG_REQUEST;

typedef struct _SOCKET_RECVMSG_REQUEST32 {
	SOCKET_WSAMSG*POINTER_32 lpMsg;
} SOCKET_RECVMSG_REQUEST32, *PSOCKET_RECVMSG_REQUEST32;

typedef struct _SOCKET_SELECT_REQUEST {
	int					fd_setsize;
	int					nfds;
	PSOCKET_FD_SET		readfds;
	PSOCKET_FD_SET		writefds;
	PSOCKET_FD_SET		exceptfds;
	SOCKET_TIMEVAL		timeout;
	BOOLEAN				infinite;
} SOCKET_SELECT_REQUEST, *PSOCKET_SELECT_REQUEST;

typedef struct _SOCKET_SELECT_REQUEST32 {
	int		fd_setsize;
	int		nfds;
	SOCKET_FD_SET32*POINTER_32		readfds;
	SOCKET_FD_SET32*POINTER_32		writefds;
	SOCKET_FD_SET32*POINTER_32		exceptfds;
	SOCKET_TIMEVAL					timeout;
	BOOLEAN							infinite;
} SOCKET_SELECT_REQUEST32, *PSOCKET_SELECT_REQUEST32;

typedef struct _SOCKET_EVENTSELECT_REQUEST {
	HANDLE		hEventObject;
	LONG		lNetworkEvents;
} SOCKET_EVENTSELECT_REQUEST, *PSOCKET_EVENTSELECT_REQUEST;

typedef struct _SOCKET_EVENTSELECT_REQUEST32 {
	ULONG		hEventObject;
	LONG		lNetworkEvents;
} SOCKET_EVENTSELECT_REQUEST32, *PSOCKET_EVENTSELECT_REQUEST32;

typedef struct _SOCKET_ENUMNETWORKEVENTS_REQUEST {
	HANDLE		hEventObject;
	SOCKET_WSANETWORKEVENTS networkEvents;
} SOCKET_ENUMNETWORKEVENTS_REQUEST, *PSOCKET_ENUMNETWORKEVENTS_REQUEST;

typedef struct _SOCKET_ENUMNETWORKEVENTS_REQUEST32 {
	ULONG		hEventObject;
	SOCKET_WSANETWORKEVENTS networkEvents;
} SOCKET_ENUMNETWORKEVENTS_REQUEST32, *PSOCKET_ENUMNETWORKEVENTS_REQUEST32;

typedef struct _SOCKET_SOCKOPT_REQUEST {
	int		 level;
	int		 optname;
	char	*optval;
	int		 optlen;
} SOCKET_SOCKOPT_REQUEST, *PSOCKET_SOCKOPT_REQUEST;

typedef struct _SOCKET_SOCKOPT_REQUEST32 {
	int				level;
	int				optname;
	char*POINTER_32	optval;
	int				optlen;
} SOCKET_SOCKOPT_REQUEST32, *PSOCKET_SOCKOPT_REQUEST32;

typedef struct _SOCKET_SCTPSEND_REQUEST {
	void			*data;
	size_t			 len;
	struct sockaddr *to;
	int				 tolen;
	struct sctp_sndrcvinfo *sinfo;
	int				 flags;
} SOCKET_SCTPSEND_REQUEST, *PSOCKET_SCTPSEND_REQUEST;

typedef struct _SOCKET_SCTPSEND_REQUEST32 {
	void*POINTER_32		data;
	ULONG				len;
	struct sockaddr*POINTER_32	to;
	int					tolen;
	struct sctp_sndrecvinfo*POINTER_32	sinfo;
	int					flags;
} SOCKET_SCTPSEND_REQUEST32, *PSOCKET_SCTPSEND_REQUEST32;

typedef struct _SOCKET_SCTPRECV_REQUEST {
	void		*data;
	size_t		len;
	struct sockaddr *from;
	int		*fromlen;
	struct sctp_sndrcvinfo *sinfo;
	int		*msg_flags;
} SOCKET_SCTPRECV_REQUEST, *PSOCKET_SCTPRECV_REQUEST;

typedef struct _SOCKET_SCTPRECV_REQUEST32 {
	void*POINTER_32	data;
	ULONG			len;
	struct sockaddr*POINTER_32 from;
	int*POINTER_32	fromlen;
	struct sctp_sndrcvinfo*POINTER_32 sinfo;
	int*POINTER_32	msg_flags;
} SOCKET_SCTPRECV_REQUEST32, *PSOCKET_SCTPRECV_REQUEST32;

typedef struct _SOCKET_PEELOFF_REQUEST {
	HANDLE		socket;
	ULONG		assoc_id;
} SOCKET_PEELOFF_REQUEST, *PSOCKET_PEELOFF_REQUEST;

typedef struct _SOCKET_PEELOFF_REQUEST32 {
	ULONG		socket;
	ULONG		assoc_id;
} SOCKET_PEELOFF_REQUEST32, *PSOCKET_PEELOFF_REQUEST32;

typedef struct _SOCKET_IOCTL_REQUEST {
	HANDLE		socket;
	DWORD		dwIoControlCode;
	PVOID		lpvInBuffer;
	DWORD		cbInBuffer;
	PVOID		lpvOutBuffer;
	DWORD		cbOutBuffer;
} SOCKET_IOCTL_REQUEST, *PSOCKET_IOCTL_REQUEST;

typedef struct _SOCKET_IOCTL_REQUEST32 {
	ULONG		socket;
	DWORD		dwIoControlCode;
	VOID*POINTER_32	lpvInBuffer;
	DWORD		cbInBuffer;
	VOID*POINTER_32 lpvOutBuffer;
	DWORD		cbOutBuffer;
} SOCKET_IOCTL_REQUEST32, *PSOCKET_IOCTL_REQUEST32;


#define FSCTL_SOCKET_BASE	FILE_DEVICE_NETWORK

#define _SOCKET_CTL_CODE(function, method, access) \
	CTL_CODE(FSCTL_SOCKET_BASE, function, method, access)

#define IOCTL_SOCKET_OPEN \
	_SOCKET_CTL_CODE(0x800, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_BIND \
	_SOCKET_CTL_CODE(0x801, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_CONNECT \
	_SOCKET_CTL_CODE(0x802, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_LISTEN \
	_SOCKET_CTL_CODE(0x803, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_ACCEPT \
	_SOCKET_CTL_CODE(0x804, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_SOCKET_SEND \
	_SOCKET_CTL_CODE(0x805, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_SENDDISCONNECT \
	_SOCKET_CTL_CODE(0x806, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_SENDTO \
	_SOCKET_CTL_CODE(0x807, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_SENDMSG \
	_SOCKET_CTL_CODE(0x808, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_RECV \
	_SOCKET_CTL_CODE(0x809, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_RECVDISCONNECT \
	_SOCKET_CTL_CODE(0x80A, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_RECVFROM \
	_SOCKET_CTL_CODE(0x80B, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_RECVMSG \
	_SOCKET_CTL_CODE(0x80C, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_SELECT \
	_SOCKET_CTL_CODE(0x80D, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_SOCKET_EVENTSELECT \
	_SOCKET_CTL_CODE(0x80E, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_SOCKET_ENUMNETWORKEVENTS \
	_SOCKET_CTL_CODE(0x80F, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_SOCKET_SETSOCKOPT \
	_SOCKET_CTL_CODE(0x810, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_GETSOCKOPT \
	_SOCKET_CTL_CODE(0x811, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_GETSOCKNAME \
	_SOCKET_CTL_CODE(0x812, METHOD_OUT_DIRECT, FILE_WRITE_DATA)

#define IOCTL_SOCKET_GETPEERNAME \
	_SOCKET_CTL_CODE(0x813, METHOD_OUT_DIRECT, FILE_WRITE_DATA)

#define IOCTL_SOCKET_SHUTDOWN \
	_SOCKET_CTL_CODE(0x814, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_SCTPSEND \
	_SOCKET_CTL_CODE(0x815, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_SCTPRECV \
	_SOCKET_CTL_CODE(0x816, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_SOCKET_PEELOFF \
	_SOCKET_CTL_CODE(0x817, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_SOCKET_IOCTL \
	_SOCKET_CTL_CODE(0x818, METHOD_BUFFERED, FILE_READ_DATA)

#pragma warning(pop)

#endif /* _SYS_SYSCALL_H_ */
