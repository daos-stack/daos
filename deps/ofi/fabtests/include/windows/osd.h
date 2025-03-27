/*
* Copyright (c) 2017 Intel Corporation.  All rights reserved.
*
* This software is available to you under the BSD license below:
*
*     Redistribution and use in source and binary forms, with or
*     without modification, are permitted provided that the following
*     conditions are met:
*
*      - Redistributions of source code must retain the above
*        copyright notice, this list of conditions and the following
*        disclaimer.
*
*      - Redistributions in binary form must reproduce the above
*        copyright notice, this list of conditions and the following
*        disclaimer in the documentation and/or other materials
*        provided with the distribution.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#ifndef _WINDOWS_OSD_H_
#define _WINDOWS_OSD_H_

#include <winsock2.h>
#include <ws2def.h>
#include <windows.h>
#include <assert.h>
#include <inttypes.h>

#include <time.h>

struct iovec
{
	void *iov_base; /* Pointer to data.  */
	size_t iov_len; /* Length of data.  */
};

#define strdup _strdup
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define alloca _alloca
#define SHUT_RDWR SD_BOTH
#define CLOCK_MONOTONIC	1

#ifndef EAI_SYSTEM
# define EAI_SYSTEM	-11
#endif

typedef int pid_t;
typedef SSIZE_T ssize_t;

/*
 * The following defines redefine the Windows Socket
 * errors as BSD errors.
 */
#ifndef ENOTEMPTY
# define ENOTEMPTY		41	/* Directory not empty */
#endif
#ifndef EREMOTE
# define EREMOTE		66	/* The object is remote */
#endif
#ifndef EPFNOSUPPORT
# define EPFNOSUPPORT		96	/* Protocol family not supported */
#endif
#ifndef EADDRINUSE
# define EADDRINUSE		100	/* Address already in use */
#endif
#ifndef EADDRNOTAVAIL
# define EADDRNOTAVAIL		101	/* Can't assign requested address */
#endif
#ifndef EAFNOSUPPORT
# define EAFNOSUPPORT		102	/* Address family not supported */
#endif
#ifndef EALREADY
# define EALREADY		103	/* Operation already in progress */
#endif
#ifndef EBADMSG
# define EBADMSG		104	/* Not a data message */
#endif
#ifndef ECANCELED
# define ECANCELED		105	/* Canceled */
#endif
#ifndef ECONNABORTED
# define ECONNABORTED		106	/* Software caused connection abort */
#endif
#ifndef ECONNREFUSED
# define ECONNREFUSED		107	/* Connection refused */
#endif
#ifndef ECONNRESET
# define ECONNRESET		108	/* Connection reset by peer */
#endif
#ifndef EDESTADDRREQ
# define EDESTADDRREQ		109	/* Destination address required */
#endif
#ifndef EHOSTUNREACH
# define EHOSTUNREACH		110	/* No route to host */
#endif
#ifndef EIDRM
# define EIDRM			111	/* Identifier removed */
#endif
#ifndef EINPROGRESS
# define EINPROGRESS		112	/* Operation now in progress */
#endif
#ifndef EISCONN
# define EISCONN		113	/* Socket is already connected */
#endif
#ifndef ELOOP
# define ELOOP			114	/* Symbolic link loop */
#endif
#ifndef EMSGSIZE
# define EMSGSIZE		115	/* Message too long */
#endif
#ifndef ENETDOWN
# define ENETDOWN		116	/* Network is down */
#endif
#ifndef ENETRESET
# define ENETRESET		117	/* Network dropped connection on reset */
#endif
#ifndef ENETUNREACH
# define ENETUNREACH		118	/* Network is unreachable */
#endif
#ifndef ENOBUFS
# define ENOBUFS		119	/* No buffer space available */
#endif
#ifndef ENODATA
# define ENODATA		120	/* No data available */
#endif
#ifndef ENOLINK
# define ENOLINK		121	/* Link has be severed */
#endif
#ifndef ENOMSG
# define ENOMSG			122	/* No message of desired type */
#endif
#ifndef ENOPROTOOPT
# define ENOPROTOOPT		123	/* Protocol not available */
#endif
#ifndef ENOSR
# define ENOSR			124	/* Out of stream resources */
#endif
#ifndef ENOSTR
# define ENOSTR			125	/* Not a stream device */
#endif
#ifndef ENOTCONN
# define ENOTCONN		126	/* Socket is not connected */
#endif
#ifndef ENOTRECOVERABLE
# define ENOTRECOVERABLE	127	/* Not recoverable */
#endif
#ifndef ENOTSOCK
# define ENOTSOCK		128	/* Socket operation on non-socket */
#endif
#ifndef ENOTSUP
# define ENOTSUP		129	/* Operation not supported */
#endif
#ifndef EOPNOTSUPP
# define EOPNOTSUPP		130	/* Operation not supported on socket */
#endif
#ifndef EOTHER
# define EOTHER			131	/* Other error */
#endif
#ifndef EOVERFLOW
# define EOVERFLOW		132	/* File too big */
#endif
#ifndef EOWNERDEAD
# define EOWNERDEAD		133	/* Owner dead */
#endif
#ifndef EPROTO
# define EPROTO			134	/* Protocol error */
#endif
#ifndef EPROTONOSUPPORT
# define EPROTONOSUPPORT	135	/* Protocol not supported */
#endif
#ifndef EPROTOTYPE
# define EPROTOTYPE		136	/* Protocol wrong type for socket */
#endif
#ifndef ETIME
# define ETIME			137	/* Timer expired */
#endif
#ifndef ETIMEDOUT
# define ETIMEDOUT		138	/* Connection timed out */
#endif
#ifndef ETXTBSY
# define ETXTBSY		139	/* Text file or pseudo-device busy */
#endif
#ifndef EWOULDBLOCK
# define EWOULDBLOCK		140	/* Operation would block */
#endif

/* Visual Studio doesn't have these, so just choose some high numbers */
#ifndef ESOCKTNOSUPPORT
# define ESOCKTNOSUPPORT	240	/* Socket type not supported */
#endif
#ifndef ESHUTDOWN
# define ESHUTDOWN		241	/* Can't send after socket shutdown */
#endif
#ifndef ETOOMANYREFS
# define ETOOMANYREFS		242	/* Too many references: can't splice */
#endif
#ifndef EHOSTDOWN
# define EHOSTDOWN		243	/* Host is down */
#endif
#ifndef EUSERS
# define EUSERS			244	/* Too many users (for UFS) */
#endif
#ifndef EDQUOT
# define EDQUOT			245	/* Disc quota exceeded */
#endif
#ifndef ESTALE
# define ESTALE			246	/* Stale NFS file handle */
#endif

/* MSG_NOSIGNAL doesn't exist on Windows */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL	0
#endif

/*
 * Win32 error code should be passed as a parameter.
 * This routine converts a Win32 error into an errno value.
 */
static unsigned char winerr2bsderr(DWORD win_errcode) /* do NOT use directly */
{
	/*
	 * The following table contains the mapping from Win32 errors to errno errors.
	 */
	static const unsigned char error_table[] = {
		0,
		EINVAL,		/* ERROR_INVALID_FUNCTION		1*/
		ENOENT,		/* ERROR_FILE_NOT_FOUND			2 */
		ENOENT,		/* ERROR_PATH_NOT_FOUND			3 */
		EMFILE,		/* ERROR_TOO_MANY_OPEN_FILES		4 */
		EACCES,		/* ERROR_ACCESS_DENIED			5 */
		EBADF,		/* ERROR_INVALID_HANDLE			6 */
		ENOMEM,		/* ERROR_ARENA_TRASHED			7 */
		ENOMEM,		/* ERROR_NOT_ENOUGH_MEMORY		8 */
		ENOMEM,		/* ERROR_INVALID_BLOCK			9 */
		E2BIG,		/* ERROR_BAD_ENVIRONMENT		10 */
		ENOEXEC,	/* ERROR_BAD_FORMAT			11 */
		EACCES,		/* ERROR_INVALID_ACCESS			12 */
		EINVAL,		/* ERROR_INVALID_DATA			13 */
		EFAULT,		/* ERROR_OUT_OF_MEMORY			14 */
		ENOENT,		/* ERROR_INVALID_DRIVE			15 */
		EACCES,		/* ERROR_CURRENT_DIRECTORY		16 */
		EXDEV,		/* ERROR_NOT_SAME_DEVICE		17 */
		ENOENT,		/* ERROR_NO_MORE_FILES			18 */
		EROFS,		/* ERROR_WRITE_PROTECT			19 */
		ENXIO,		/* ERROR_BAD_UNIT			20 */
		EBUSY,		/* ERROR_NOT_READY			21 */
		EIO,		/* ERROR_BAD_COMMAND			22 */
		EIO,		/* ERROR_CRC				23 */
		EIO,		/* ERROR_BAD_LENGTH			24 */
		EIO,		/* ERROR_SEEK				25 */
		EIO,		/* ERROR_NOT_DOS_DISK			26 */
		ENXIO,		/* ERROR_SECTOR_NOT_FOUND		27 */
		EBUSY,		/* ERROR_OUT_OF_PAPER			28 */
		EIO,		/* ERROR_WRITE_FAULT			29 */
		EIO,		/* ERROR_READ_FAULT			30 */
		EIO,		/* ERROR_GEN_FAILURE			31 */
		EACCES,		/* ERROR_SHARING_VIOLATION		32 */
		EACCES,		/* ERROR_LOCK_VIOLATION			33 */
		ENXIO,		/* ERROR_WRONG_DISK			34 */
		ENFILE,		/* ERROR_FCB_UNAVAILABLE		35 */
		ENFILE,		/* ERROR_SHARING_BUFFER_EXCEEDED	36 */
		EINVAL,		/*					37 */
		EINVAL,		/*					38 */
		ENOSPC,		/* ERROR_HANDLE_DISK_FULL		39 */
		EINVAL,		/*					40 */
		EINVAL,		/*					41 */
		EINVAL,		/*					42 */
		EINVAL,		/*					43 */
		EINVAL,		/*					44 */
		EINVAL,		/*					45 */
		EINVAL,		/*					46 */
		EINVAL,		/*					47 */
		EINVAL,		/*					48 */
		EINVAL,		/*					49 */
		ENODEV,		/* ERROR_NOT_SUPPORTED			50 */
		EBUSY,		/* ERROR_REM_NOT_LIST			51 */
		EEXIST,		/* ERROR_DUP_NAME			52 */
		ENOENT,		/* ERROR_BAD_NETPATH			53 */
		EBUSY,		/* ERROR_NETWORK_BUSY			54 */
		ENODEV,		/* ERROR_DEV_NOT_EXIST			55 */
		EAGAIN,		/* ERROR_TOO_MANY_CMDS			56 */
		EIO,		/* ERROR_ADAP_HDW_ERR			57 */
		EIO,		/* ERROR_BAD_NET_RESP			58 */
		EIO,		/* ERROR_UNEXP_NET_ERR			59 */
		EINVAL,		/* ERROR_BAD_REM_ADAP			60 */
		EFBIG,		/* ERROR_PRINTQ_FULL			61 */
		ENOSPC,		/* ERROR_NO_SPOOL_SPACE			62 */
		ENOENT,		/* ERROR_PRINT_CANCELLED		63 */
		ENOENT,		/* ERROR_NETNAME_DELETED		64 */
		EACCES,		/* ERROR_NETWORK_ACCESS_DENIED		65 */
		ENODEV,		/* ERROR_BAD_DEV_TYPE			66 */
		ENOENT,		/* ERROR_BAD_NET_NAME			67 */
		ENFILE,		/* ERROR_TOO_MANY_NAMES			68 */
		EIO,		/* ERROR_TOO_MANY_SESS			69 */
		EAGAIN,		/* ERROR_SHARING_PAUSED			70 */
		EINVAL,		/* ERROR_REQ_NOT_ACCEP			71 */
		EAGAIN,		/* ERROR_REDIR_PAUSED			72 */
		EINVAL,		/*					73 */
		EINVAL,		/*					74 */
		EINVAL,		/*					75 */
		EINVAL,		/*					76 */
		EINVAL,		/*					77 */
		EINVAL,		/*					78 */
		EINVAL,		/*					79 */
		EEXIST,		/* ERROR_FILE_EXISTS			80 */
		EINVAL,		/*					81 */
		ENOSPC,		/* ERROR_CANNOT_MAKE			82 */
		EIO,		/* ERROR_FAIL_I24			83 */
		ENFILE,		/* ERROR_OUT_OF_STRUCTURES		84 */
		EEXIST,		/* ERROR_ALREADY_ASSIGNED		85 */
		EPERM,		/* ERROR_INVALID_PASSWORD		86 */
		EINVAL,		/* ERROR_INVALID_PARAMETER		87 */
		EIO,		/* ERROR_NET_WRITE_FAULT		88 */
		EAGAIN,		/* ERROR_NO_PROC_SLOTS			89 */
		EINVAL,		/*					90 */
		EINVAL,		/*					91 */
		EINVAL,		/*					92 */
		EINVAL,		/*					93 */
		EINVAL,		/*					94 */
		EINVAL,		/*					95 */
		EINVAL,		/*					96 */
		EINVAL,		/*					97 */
		EINVAL,		/*					98 */
		EINVAL,		/*					99 */
		EINVAL,		/*					100 */
		EINVAL,		/*					101 */
		EINVAL,		/*					102 */
		EINVAL,		/*					103 */
		EINVAL,		/*					104 */
		EINVAL,		/*					105 */
		EINVAL,		/*					106 */
		EXDEV,		/* ERROR_DISK_CHANGE			107 */
		EAGAIN,		/* ERROR_DRIVE_LOCKED			108 */
		EPIPE,		/* ERROR_BROKEN_PIPE			109 */
		ENOENT,		/* ERROR_OPEN_FAILED			110 */
		EINVAL,		/* ERROR_BUFFER_OVERFLOW		111 */
		ENOSPC,		/* ERROR_DISK_FULL			112 */
		EMFILE,		/* ERROR_NO_MORE_SEARCH_HANDLES		113 */
		EBADF,		/* ERROR_INVALID_TARGET_HANDLE		114 */
		EFAULT,		/* ERROR_PROTECTION_VIOLATION		115 */
		EINVAL,		/*					116 */
		EINVAL,		/*					117 */
		EINVAL,		/*					118 */
		EINVAL,		/*					119 */
		EINVAL,		/*					120 */
		EINVAL,		/*					121 */
		EINVAL,		/*					122 */
		ENOENT,		/* ERROR_INVALID_NAME			123 */
		EINVAL,		/*					124 */
		EINVAL,		/*					125 */
		EINVAL,		/*					126 */
		EINVAL,		/* ERROR_PROC_NOT_FOUND			127 */
		ECHILD,		/* ERROR_WAIT_NO_CHILDREN		128 */
		ECHILD,		/* ERROR_CHILD_NOT_COMPLETE		129 */
		EBADF,		/* ERROR_DIRECT_ACCESS_HANDLE		130 */
		EINVAL,		/* ERROR_NEGATIVE_SEEK			131 */
		ESPIPE,		/* ERROR_SEEK_ON_DEVICE			132 */
		EINVAL,		/*					133 */
		EINVAL,		/*					134 */
		EINVAL,		/*					135 */
		EINVAL,		/*					136 */
		EINVAL,		/*					137 */
		EINVAL,		/*					138 */
		EINVAL,		/*					139 */
		EINVAL,		/*					140 */
		EINVAL,		/*					141 */
		EAGAIN,		/* ERROR_BUSY_DRIVE			142 */
		EINVAL,		/*					143 */
		EINVAL,		/*					144 */
		EEXIST,		/* ERROR_DIR_NOT_EMPTY			145 */
		EINVAL,		/*					146 */
		EINVAL,		/*					147 */
		EINVAL,		/*					148 */
		EINVAL,		/*					149 */
		EINVAL,		/*					150 */
		EINVAL,		/*					151 */
		EINVAL,		/*					152 */
		EINVAL,		/*					153 */
		EINVAL,		/*					154 */
		EINVAL,		/*					155 */
		EINVAL,		/*					156 */
		EINVAL,		/*					157 */
		EACCES,		/* ERROR_NOT_LOCKED			158 */
		EINVAL,		/*					159 */
		EINVAL,		/*					160 */
		ENOENT,		/* ERROR_BAD_PATHNAME			161 */
		EINVAL,		/*					162 */
		EINVAL,		/*					163 */
		EINVAL,		/*					164 */
		EINVAL,		/*					165 */
		EINVAL,		/*					166 */
		EACCES,		/* ERROR_LOCK_FAILED			167 */
		EINVAL,		/*					168 */
		EINVAL,		/*					169 */
		EINVAL,		/*					170 */
		EINVAL,		/*					171 */
		EINVAL,		/*					172 */
		EINVAL,		/*					173 */
		EINVAL,		/*					174 */
		EINVAL,		/*					175 */
		EINVAL,		/*					176 */
		EINVAL,		/*					177 */
		EINVAL,		/*					178 */
		EINVAL,		/*					179 */
		EINVAL,		/*					180 */
		EINVAL,		/*					181 */
		EINVAL,		/*					182 */
		EEXIST,		/* ERROR_ALREADY_EXISTS			183 */
		ECHILD,		/* ERROR_NO_CHILD_PROCESS		184 */
		EINVAL,		/*					185 */
		EINVAL,		/*					186 */
		EINVAL,		/*					187 */
		EINVAL,		/*					188 */
		EINVAL,		/*					189 */
		EINVAL,		/*					190 */
		EINVAL,		/*					191 */
		EINVAL,		/*					192 */
		EINVAL,		/*					193 */
		EINVAL,		/*					194 */
		EINVAL,		/*					195 */
		EINVAL,		/*					196 */
		EINVAL,		/*					197 */
		EINVAL,		/*					198 */
		EINVAL,		/*					199 */
		EINVAL,		/*					200 */
		EINVAL,		/*					201 */
		EINVAL,		/*					202 */
		EINVAL,		/*					203 */
		EINVAL,		/*					204 */
		EINVAL,		/*					205 */
		ENAMETOOLONG,	/* ERROR_FILENAME_EXCED_RANGE		206 */
		EINVAL,		/*					207 */
		EINVAL,		/*					208 */
		EINVAL,		/*					209 */
		EINVAL,		/*					210 */
		EINVAL,		/*					211 */
		EINVAL,		/*					212 */
		EINVAL,		/*					213 */
		EINVAL,		/*					214 */
		EINVAL,		/*					215 */
		EINVAL,		/*					216 */
		EINVAL,		/*					217 */
		EINVAL,		/*					218 */
		EINVAL,		/*					219 */
		EINVAL,		/*					220 */
		EINVAL,		/*					221 */
		EINVAL,		/*					222 */
		EINVAL,		/*					223 */
		EINVAL,		/*					224 */
		EINVAL,		/*					225 */
		EINVAL,		/*					226 */
		EINVAL,		/*					227 */
		EINVAL,		/*					228 */
		EINVAL,		/*					229 */
		EPIPE,		/* ERROR_BAD_PIPE			230 */
		EAGAIN,		/* ERROR_PIPE_BUSY			231 */
		EPIPE,		/* ERROR_NO_DATA			232 */
		EPIPE,		/* ERROR_PIPE_NOT_CONNECTED		233 */
		EINVAL,		/*					234 */
		EINVAL,		/*					235 */
		EINVAL,		/*					236 */
		EINVAL,		/*					237 */
		EINVAL,		/*					238 */
		EINVAL,		/*					239 */
		EINVAL,		/*					240 */
		EINVAL,		/*					241 */
		EINVAL,		/*					242 */
		EINVAL,		/*					243 */
		EINVAL,		/*					244 */
		EINVAL,		/*					245 */
		EINVAL,		/*					246 */
		EINVAL,		/*					247 */
		EINVAL,		/*					248 */
		EINVAL,		/*					249 */
		EINVAL,		/*					250 */
		EINVAL,		/*					251 */
		EINVAL,		/*					252 */
		EINVAL,		/*					253 */
		EINVAL,		/*					254 */
		EINVAL,		/*					255 */
		EINVAL,		/*					256 */
		EINVAL,		/*					257 */
		EINVAL,		/*					258 */
		EINVAL,		/*					259 */
		EINVAL,		/*					260 */
		EINVAL,		/*					261 */
		EINVAL,		/*					262 */
		EINVAL,		/*					263 */
		EINVAL,		/*					264 */
		EINVAL,		/*					265 */
		EINVAL,		/*					266 */
		ENOTDIR		/* ERROR_DIRECTORY			267 */
	};

	/*
	* The following table contains the mapping from WinSock errors to
	* errno errors.
	*/
	static const unsigned char wsa_error_table[] = {
		EWOULDBLOCK,		/* WSAEWOULDBLOCK */
		EINPROGRESS,		/* WSAEINPROGRESS */
		EALREADY,		/* WSAEALREADY */
		ENOTSOCK,		/* WSAENOTSOCK */
		EDESTADDRREQ,		/* WSAEDESTADDRREQ */
		EMSGSIZE,		/* WSAEMSGSIZE */
		EPROTOTYPE,		/* WSAEPROTOTYPE */
		ENOPROTOOPT,		/* WSAENOPROTOOPT */
		EPROTONOSUPPORT,	/* WSAEPROTONOSUPPORT */
		ESOCKTNOSUPPORT,	/* WSAESOCKTNOSUPPORT */
		EOPNOTSUPP,		/* WSAEOPNOTSUPP */
		EPFNOSUPPORT,		/* WSAEPFNOSUPPORT */
		EAFNOSUPPORT,		/* WSAEAFNOSUPPORT */
		EADDRINUSE,		/* WSAEADDRINUSE */
		EADDRNOTAVAIL,		/* WSAEADDRNOTAVAIL */
		ENETDOWN,		/* WSAENETDOWN */
		ENETUNREACH,		/* WSAENETUNREACH */
		ENETRESET,		/* WSAENETRESET */
		ECONNABORTED,		/* WSAECONNABORTED */
		ECONNRESET,		/* WSAECONNRESET */
		ENOBUFS,		/* WSAENOBUFS */
		EISCONN,		/* WSAEISCONN */
		ENOTCONN,		/* WSAENOTCONN */
		ESHUTDOWN,		/* WSAESHUTDOWN */
		ETOOMANYREFS,		/* WSAETOOMANYREFS */
		ETIMEDOUT,		/* WSAETIMEDOUT */
		ECONNREFUSED,		/* WSAECONNREFUSED */
		ELOOP,			/* WSAELOOP */
		ENAMETOOLONG,		/* WSAENAMETOOLONG */
		EHOSTDOWN,		/* WSAEHOSTDOWN */
		EHOSTUNREACH,		/* WSAEHOSTUNREACH */
		ENOTEMPTY,		/* WSAENOTEMPTY */
		EAGAIN,			/* WSAEPROCLIM */
		EUSERS,			/* WSAEUSERS */
		EDQUOT,			/* WSAEDQUOT */
		ESTALE,			/* WSAESTALE */
		EREMOTE			/* WSAEREMOTE */
	};

	if (win_errcode >= sizeof(error_table) / sizeof(error_table[0])) {
		win_errcode -= WSAEWOULDBLOCK;
		if (win_errcode >= (sizeof(wsa_error_table) / sizeof(wsa_error_table[0]))) {
			return error_table[1];
		}
		else {
			return wsa_error_table[win_errcode];
		}
	}
	else {
		return error_table[win_errcode];
	}
}

/*
 * The FILETIME structure records time in the form of
 * 100-nanosecond intervals since January 1, 1601
 */
#define file2unix_time	10000000i64		/* 1E+7 */
#define win2unix_epoch	116444736000000000i64	/* 1 Jan 1601 to 1 Jan 1970 */

static inline
int clock_gettime(int which_clock, struct timespec *spec)
{
	__int64 wintime;

	GetSystemTimeAsFileTime((FILETIME*)&wintime);
	wintime -= win2unix_epoch;

	spec->tv_sec = wintime / file2unix_time;
	spec->tv_nsec = wintime % file2unix_time * 100;

	return 0;
}

static inline int ft_close_fd(int fd)
{
	return closesocket(fd);
}

static inline int poll(struct pollfd *fds, int nfds, int timeout)
{
	return WSAPoll(fds, nfds, timeout);
}

static inline char* strndup(const char* str, size_t n)
{
	char* res = strdup(str);
	if (strlen(res) > n)
		res[n] = '\0';
	return res;
}

static inline char* strsep(char **stringp, const char *delim)
{
	char* ptr = *stringp;
	char* p;

	p = ptr ? strpbrk(ptr, delim) : NULL;

	if(!p)
		*stringp = NULL;
	else
	{
		*p = 0;
		*stringp = p + 1;
	}

	return ptr;
}

static inline char *strtok_r(char *str, const char *delimiters, char **saveptr)
{
	return strtok_s(str, delimiters, saveptr);
}

#define _SC_PAGESIZE	30

static long int sysconf(int name)
{
	switch (name) {
	case _SC_PAGESIZE:
		SYSTEM_INFO info;
		GetNativeSystemInfo(&info);
		return (long int)info.dwPageSize;
	default:
		assert(0);
	}
	errno = EINVAL;
	return -1;
}

#define AF_LOCAL AF_UNIX

int socketpair(int af, int type, int protocol, int socks[2]);

static inline int ft_fd_nonblock(int fd)
{
	u_long argp = 1;
	return ioctlsocket(fd, FIONBIO, &argp) ? -WSAGetLastError() : 0;
}

/* Note: Use static variable `errno` for libc routines
 * (such as fopen, lseek and etc)
 * If you need to define which function/variable is needed
 * to get correct `errno`, cosult with MSDN pages */
/*
 * Use only for OFI wrappers that use Windows Socket API (WSA):
 * Socket routines, poll and etc
 */
static inline int ofi_sockerr(void)
{
	return winerr2bsderr(WSAGetLastError());
}

/* Bits in the fourth argument to `waitid'.  */
#define WSTOPPED	2	/* Report stopped child (same as WUNTRACED). */
#define WEXITED		4	/* Report dead child. */
#define WCONTINUED	8	/* Report continued child. */
#define WNOWAIT		0x01000000	/* Don't reap, just poll status. */

static pid_t waitpid(pid_t pid, int *status, int options)
{
	assert(0);
	return 0;
}

static const char* gai_strerror(int code)
{
	return "Unknown error";
}

static pid_t fork(void)
{
	assert(0);
	return -1;
}

static int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	*memptr = _aligned_malloc(size, alignment);
	return (*memptr) ? 0 : ENOMEM;
}

static inline int ft_startup(void)
{
	int ret = 0;
	WSADATA data;

	ret = WSAStartup(MAKEWORD(2, 2), &data);
	if (ret)
		return HRESULT_FROM_WIN32(ret);
	return ret;
}

/*
 * The windows API limits socket send/recv transfers to INT_MAX.
 * For nonblocking, stream sockets, we limit send/recv calls to that
 * size, since the sockets aren't guaranteed to send the full amount
 * requested.  For datagram sockets, we don't expect any transfers to
 * be larger than a few KB.
 * We do not handle blocking sockets that attempt to transfer more
 * than INT_MAX data at a time.
 */
static inline ssize_t
ofi_recv_socket(SOCKET fd, void *buf, size_t count, int flags)
{
	int len = count > INT_MAX ? INT_MAX : (int) count;
	return (ssize_t) recv(fd, (char *) buf, len, flags);
}

static inline ssize_t
ofi_send_socket(SOCKET fd, const void *buf, size_t count, int flags)
{
	int len = count > INT_MAX ? INT_MAX : (int) count;
	return (ssize_t) send(fd, (const char*) buf, len, flags);
}


/* complex operations implementation */
#define OFI_COMPLEX(name) ofi_##name##_complex
#define OFI_COMPLEX_BASE(name) OFI_COMPLEX(name)##_base
#define OFI_COMPLEX_OP(name, op) ofi_complex_##name##_##op
#define OFI_COMPLEX_TYPE_DECL(name, type)	\
typedef type OFI_COMPLEX_BASE(name);		\
typedef struct {				\
	OFI_COMPLEX_BASE(name) re;		\
	OFI_COMPLEX_BASE(name) im;		\
} OFI_COMPLEX(name);

OFI_COMPLEX_TYPE_DECL(float, float)
OFI_COMPLEX_TYPE_DECL(double, double)
OFI_COMPLEX_TYPE_DECL(long_double, long double)

#define OFI_COMPLEX_OPS(name)								\
static inline OFI_COMPLEX_BASE(name) OFI_COMPLEX_OP(name, real)(OFI_COMPLEX(name) v)	\
{											\
	return v.re;									\
} 											\
static inline OFI_COMPLEX_BASE(name) OFI_COMPLEX_OP(name, imag)(OFI_COMPLEX(name) v)	\
{											\
	return v.im;									\
}											\
static inline OFI_COMPLEX(name) OFI_COMPLEX_OP(name, sum)(OFI_COMPLEX(name) v1, OFI_COMPLEX(name) v2) \
{											\
	OFI_COMPLEX(name) ret = {.re = v1.re + v2.re, .im = v1.im + v2.im};		\
	return ret;									\
}											\
static inline OFI_COMPLEX(name) OFI_COMPLEX_OP(name, mul)(OFI_COMPLEX(name) v1, OFI_COMPLEX(name) v2) \
{											\
	OFI_COMPLEX(name) ret = {.re = (v1.re * v2.re) - (v1.im * v2.im),		\
			      .im = (v1.re * v2.im) + (v1.im * v2.re)};			\
	return ret;									\
}											\
static inline int OFI_COMPLEX_OP(name, equ)(OFI_COMPLEX(name) v1, OFI_COMPLEX(name) v2)	\
{											\
	return v1.re == v2.re && v1.im == v2.im;					\
}											\
static inline OFI_COMPLEX(name) OFI_COMPLEX_OP(name, land)(OFI_COMPLEX(name) v1, OFI_COMPLEX(name) v2) \
{											\
	OFI_COMPLEX(name) zero = {.re = 0, .im = 0};					\
	int equ = !OFI_COMPLEX_OP(name, equ)(v1, zero) && !OFI_COMPLEX_OP(name, equ)(v2, zero); \
	OFI_COMPLEX(name) ret = {.re = equ ? 1.f : 0, .im = 0};				\
	return ret;									\
}											\
static inline OFI_COMPLEX(name) OFI_COMPLEX_OP(name, lor)(OFI_COMPLEX(name) v1, OFI_COMPLEX(name) v2) \
{											\
	OFI_COMPLEX(name) zero = {.re = 0, .im = 0};					\
	int equ = !OFI_COMPLEX_OP(name, equ)(v1, zero) || !OFI_COMPLEX_OP(name, equ)(v2, zero); \
	OFI_COMPLEX(name) ret = {.re = equ ? 1.f : 0, .im = 0};				\
	return ret;									\
}

OFI_COMPLEX_OPS(float)
OFI_COMPLEX_OPS(double)
OFI_COMPLEX_OPS(long_double)

#endif /* _WINDOWS_OSD_H_ */
