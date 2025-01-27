// cgo -godefs types_freebsd.go | go run mkpost.go
// Code generated by the command above; see README.md. DO NOT EDIT.

//go:build 386 && freebsd

package unix

const (
	SizeofPtr      = 0x4
	SizeofShort    = 0x2
	SizeofInt      = 0x4
	SizeofLong     = 0x4
	SizeofLongLong = 0x8
)

type (
	_C_short     int16
	_C_int       int32
	_C_long      int32
	_C_long_long int64
)

type Timespec struct {
	Sec  int32
	Nsec int32
}

type Timeval struct {
	Sec  int32
	Usec int32
}

type Time_t int32

type Rusage struct {
	Utime    Timeval
	Stime    Timeval
	Maxrss   int32
	Ixrss    int32
	Idrss    int32
	Isrss    int32
	Minflt   int32
	Majflt   int32
	Nswap    int32
	Inblock  int32
	Oublock  int32
	Msgsnd   int32
	Msgrcv   int32
	Nsignals int32
	Nvcsw    int32
	Nivcsw   int32
}

type Rlimit struct {
	Cur int64
	Max int64
}

type _Gid_t uint32

const (
	_statfsVersion = 0x20140518
	_dirblksiz     = 0x400
)

type Stat_t struct {
	Dev     uint64
	Ino     uint64
	Nlink   uint64
	Mode    uint16
	_0      int16
	Uid     uint32
	Gid     uint32
	_1      int32
	Rdev    uint64
	_       int32
	Atim    Timespec
	_       int32
	Mtim    Timespec
	_       int32
	Ctim    Timespec
	_       int32
	Btim    Timespec
	Size    int64
	Blocks  int64
	Blksize int32
	Flags   uint32
	Gen     uint64
	Spare   [10]uint64
}

type Statfs_t struct {
	Version     uint32
	Type        uint32
	Flags       uint64
	Bsize       uint64
	Iosize      uint64
	Blocks      uint64
	Bfree       uint64
	Bavail      int64
	Files       uint64
	Ffree       int64
	Syncwrites  uint64
	Asyncwrites uint64
	Syncreads   uint64
	Asyncreads  uint64
	Spare       [10]uint64
	Namemax     uint32
	Owner       uint32
	Fsid        Fsid
	Charspare   [80]int8
	Fstypename  [16]byte
	Mntfromname [1024]byte
	Mntonname   [1024]byte
}

type Flock_t struct {
	Start  int64
	Len    int64
	Pid    int32
	Type   int16
	Whence int16
	Sysid  int32
}

type Dirent struct {
	Fileno uint64
	Off    int64
	Reclen uint16
	Type   uint8
	Pad0   uint8
	Namlen uint16
	Pad1   uint16
	Name   [256]int8
}

type Fsid struct {
	Val [2]int32
}

const (
	PathMax = 0x400
)

const (
	FADV_NORMAL     = 0x0
	FADV_RANDOM     = 0x1
	FADV_SEQUENTIAL = 0x2
	FADV_WILLNEED   = 0x3
	FADV_DONTNEED   = 0x4
	FADV_NOREUSE    = 0x5
)

type RawSockaddrInet4 struct {
	Len    uint8
	Family uint8
	Port   uint16
	Addr   [4]byte /* in_addr */
	Zero   [8]int8
}

type RawSockaddrInet6 struct {
	Len      uint8
	Family   uint8
	Port     uint16
	Flowinfo uint32
	Addr     [16]byte /* in6_addr */
	Scope_id uint32
}

type RawSockaddrUnix struct {
	Len    uint8
	Family uint8
	Path   [104]int8
}

type RawSockaddrDatalink struct {
	Len    uint8
	Family uint8
	Index  uint16
	Type   uint8
	Nlen   uint8
	Alen   uint8
	Slen   uint8
	Data   [46]int8
}

type RawSockaddr struct {
	Len    uint8
	Family uint8
	Data   [14]int8
}

type RawSockaddrAny struct {
	Addr RawSockaddr
	Pad  [92]int8
}

type _Socklen uint32

type Xucred struct {
	Version uint32
	Uid     uint32
	Ngroups int16
	Groups  [16]uint32
	_       *byte
}

type Linger struct {
	Onoff  int32
	Linger int32
}

type Iovec struct {
	Base *byte
	Len  uint32
}

type IPMreq struct {
	Multiaddr [4]byte /* in_addr */
	Interface [4]byte /* in_addr */
}

type IPMreqn struct {
	Multiaddr [4]byte /* in_addr */
	Address   [4]byte /* in_addr */
	Ifindex   int32
}

type IPv6Mreq struct {
	Multiaddr [16]byte /* in6_addr */
	Interface uint32
}

type Msghdr struct {
	Name       *byte
	Namelen    uint32
	Iov        *Iovec
	Iovlen     int32
	Control    *byte
	Controllen uint32
	Flags      int32
}

type Cmsghdr struct {
	Len   uint32
	Level int32
	Type  int32
}

type Inet6Pktinfo struct {
	Addr    [16]byte /* in6_addr */
	Ifindex uint32
}

type IPv6MTUInfo struct {
	Addr RawSockaddrInet6
	Mtu  uint32
}

type ICMPv6Filter struct {
	Filt [8]uint32
}

const (
	SizeofSockaddrInet4    = 0x10
	SizeofSockaddrInet6    = 0x1c
	SizeofSockaddrAny      = 0x6c
	SizeofSockaddrUnix     = 0x6a
	SizeofSockaddrDatalink = 0x36
	SizeofXucred           = 0x50
	SizeofLinger           = 0x8
	SizeofIovec            = 0x8
	SizeofIPMreq           = 0x8
	SizeofIPMreqn          = 0xc
	SizeofIPv6Mreq         = 0x14
	SizeofMsghdr           = 0x1c
	SizeofCmsghdr          = 0xc
	SizeofInet6Pktinfo     = 0x14
	SizeofIPv6MTUInfo      = 0x20
	SizeofICMPv6Filter     = 0x20
)

const (
	PTRACE_TRACEME = 0x0
	PTRACE_CONT    = 0x7
	PTRACE_KILL    = 0x8
)

type PtraceLwpInfoStruct struct {
	Lwpid        int32
	Event        int32
	Flags        int32
	Sigmask      Sigset_t
	Siglist      Sigset_t
	Siginfo      __PtraceSiginfo
	Tdname       [20]int8
	Child_pid    int32
	Syscall_code uint32
	Syscall_narg uint32
}

type __Siginfo struct {
	Signo  int32
	Errno  int32
	Code   int32
	Pid    int32
	Uid    uint32
	Status int32
	Addr   *byte
	Value  [4]byte
	_      [32]byte
}
type __PtraceSiginfo struct {
	Signo  int32
	Errno  int32
	Code   int32
	Pid    int32
	Uid    uint32
	Status int32
	Addr   uintptr
	Value  [4]byte
	_      [32]byte
}

type Sigset_t struct {
	Val [4]uint32
}

type Reg struct {
	Fs     uint32
	Es     uint32
	Ds     uint32
	Edi    uint32
	Esi    uint32
	Ebp    uint32
	Isp    uint32
	Ebx    uint32
	Edx    uint32
	Ecx    uint32
	Eax    uint32
	Trapno uint32
	Err    uint32
	Eip    uint32
	Cs     uint32
	Eflags uint32
	Esp    uint32
	Ss     uint32
	Gs     uint32
}

type FpReg struct {
	Env   [7]uint32
	Acc   [8][10]uint8
	Ex_sw uint32
	Pad   [64]uint8
}

type FpExtendedPrecision struct{}

type PtraceIoDesc struct {
	Op   int32
	Offs uintptr
	Addr *byte
	Len  uint32
}

type Kevent_t struct {
	Ident  uint32
	Filter int16
	Flags  uint16
	Fflags uint32
	Data   int64
	Udata  *byte
	Ext    [4]uint64
}

type FdSet struct {
	Bits [32]uint32
}

const (
	sizeofIfMsghdr         = 0xa8
	SizeofIfMsghdr         = 0x60
	sizeofIfData           = 0x98
	SizeofIfData           = 0x50
	SizeofIfaMsghdr        = 0x14
	SizeofIfmaMsghdr       = 0x10
	SizeofIfAnnounceMsghdr = 0x18
	SizeofRtMsghdr         = 0x5c
	SizeofRtMetrics        = 0x38
)

type ifMsghdr struct {
	Msglen  uint16
	Version uint8
	Type    uint8
	Addrs   int32
	Flags   int32
	Index   uint16
	_       uint16
	Data    ifData
}

type IfMsghdr struct {
	Msglen  uint16
	Version uint8
	Type    uint8
	Addrs   int32
	Flags   int32
	Index   uint16
	Data    IfData
}

type ifData struct {
	Type       uint8
	Physical   uint8
	Addrlen    uint8
	Hdrlen     uint8
	Link_state uint8
	Vhid       uint8
	Datalen    uint16
	Mtu        uint32
	Metric     uint32
	Baudrate   uint64
	Ipackets   uint64
	Ierrors    uint64
	Opackets   uint64
	Oerrors    uint64
	Collisions uint64
	Ibytes     uint64
	Obytes     uint64
	Imcasts    uint64
	Omcasts    uint64
	Iqdrops    uint64
	Oqdrops    uint64
	Noproto    uint64
	Hwassist   uint64
	_          [8]byte
	_          [16]byte
}

type IfData struct {
	Type        uint8
	Physical    uint8
	Addrlen     uint8
	Hdrlen      uint8
	Link_state  uint8
	Spare_char1 uint8
	Spare_char2 uint8
	Datalen     uint8
	Mtu         uint32
	Metric      uint32
	Baudrate    uint32
	Ipackets    uint32
	Ierrors     uint32
	Opackets    uint32
	Oerrors     uint32
	Collisions  uint32
	Ibytes      uint32
	Obytes      uint32
	Imcasts     uint32
	Omcasts     uint32
	Iqdrops     uint32
	Noproto     uint32
	Hwassist    uint32
	Epoch       int32
	Lastchange  Timeval
}

type IfaMsghdr struct {
	Msglen  uint16
	Version uint8
	Type    uint8
	Addrs   int32
	Flags   int32
	Index   uint16
	_       uint16
	Metric  int32
}

type IfmaMsghdr struct {
	Msglen  uint16
	Version uint8
	Type    uint8
	Addrs   int32
	Flags   int32
	Index   uint16
	_       uint16
}

type IfAnnounceMsghdr struct {
	Msglen  uint16
	Version uint8
	Type    uint8
	Index   uint16
	Name    [16]int8
	What    uint16
}

type RtMsghdr struct {
	Msglen  uint16
	Version uint8
	Type    uint8
	Index   uint16
	_       uint16
	Flags   int32
	Addrs   int32
	Pid     int32
	Seq     int32
	Errno   int32
	Fmask   int32
	Inits   uint32
	Rmx     RtMetrics
}

type RtMetrics struct {
	Locks    uint32
	Mtu      uint32
	Hopcount uint32
	Expire   uint32
	Recvpipe uint32
	Sendpipe uint32
	Ssthresh uint32
	Rtt      uint32
	Rttvar   uint32
	Pksent   uint32
	Weight   uint32
	Filler   [3]uint32
}

const (
	SizeofBpfVersion    = 0x4
	SizeofBpfStat       = 0x8
	SizeofBpfZbuf       = 0xc
	SizeofBpfProgram    = 0x8
	SizeofBpfInsn       = 0x8
	SizeofBpfHdr        = 0x14
	SizeofBpfZbufHeader = 0x20
)

type BpfVersion struct {
	Major uint16
	Minor uint16
}

type BpfStat struct {
	Recv uint32
	Drop uint32
}

type BpfZbuf struct {
	Bufa   *byte
	Bufb   *byte
	Buflen uint32
}

type BpfProgram struct {
	Len   uint32
	Insns *BpfInsn
}

type BpfInsn struct {
	Code uint16
	Jt   uint8
	Jf   uint8
	K    uint32
}

type BpfHdr struct {
	Tstamp  Timeval
	Caplen  uint32
	Datalen uint32
	Hdrlen  uint16
	_       [2]byte
}

type BpfZbufHeader struct {
	Kernel_gen uint32
	Kernel_len uint32
	User_gen   uint32
	_          [5]uint32
}

type Termios struct {
	Iflag  uint32
	Oflag  uint32
	Cflag  uint32
	Lflag  uint32
	Cc     [20]uint8
	Ispeed uint32
	Ospeed uint32
}

type Winsize struct {
	Row    uint16
	Col    uint16
	Xpixel uint16
	Ypixel uint16
}

const (
	AT_FDCWD            = -0x64
	AT_EACCESS          = 0x100
	AT_SYMLINK_NOFOLLOW = 0x200
	AT_SYMLINK_FOLLOW   = 0x400
	AT_REMOVEDIR        = 0x800
)

type PollFd struct {
	Fd      int32
	Events  int16
	Revents int16
}

const (
	POLLERR      = 0x8
	POLLHUP      = 0x10
	POLLIN       = 0x1
	POLLINIGNEOF = 0x2000
	POLLNVAL     = 0x20
	POLLOUT      = 0x4
	POLLPRI      = 0x2
	POLLRDBAND   = 0x80
	POLLRDNORM   = 0x40
	POLLWRBAND   = 0x100
	POLLWRNORM   = 0x4
	POLLRDHUP    = 0x4000
)

type CapRights struct {
	Rights [2]uint64
}

type Utsname struct {
	Sysname  [256]byte
	Nodename [256]byte
	Release  [256]byte
	Version  [256]byte
	Machine  [256]byte
}

const SizeofClockinfo = 0x14

type Clockinfo struct {
	Hz     int32
	Tick   int32
	Spare  int32
	Stathz int32
	Profhz int32
}
