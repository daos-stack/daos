#
# Copyright (c) 2021 Nutanix Inc. All rights reserved.
#
# Authors: John Levon <john.levon@nutanix.com>
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#      * Redistributions in binary form must reproduce the above copyright
#        notice, this list of conditions and the following disclaimer in the
#        documentation and/or other materials provided with the distribution.
#      * Neither the name of Nutanix nor the names of its contributors may be
#        used to endorse or promote products derived from this software without
#        specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
#  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
#  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
#  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
#  DAMAGE.
#

#
# Note that we don't use enum here, as class.value is a little verbose
#

from types import SimpleNamespace
import ctypes as c
import array
import errno
import json
import mmap
import os
import socket
import struct
import syslog
import copy
import tempfile

UINT64_MAX = 18446744073709551615

# from linux/pci_regs.h and linux/pci_defs.h

PCI_HEADER_TYPE_NORMAL = 0

PCI_STD_HEADER_SIZEOF = 64

PCI_BARS_NR = 6

PCI_PM_SIZEOF = 8

PCI_CFG_SPACE_SIZE = 256
PCI_CFG_SPACE_EXP_SIZE = 4096

PCI_CAP_LIST_NEXT = 1

PCI_CAP_ID_PM = 0x1
PCI_CAP_ID_VNDR = 0x9
PCI_CAP_ID_MSIX = 0x11
PCI_CAP_ID_EXP = 0x10

PCI_EXP_DEVCTL2 = 40
PCI_EXP_LNKCTL2 = 48

PCI_EXT_CAP_ID_DSN = 0x03
PCI_EXT_CAP_ID_VNDR = 0x0b

PCI_EXT_CAP_DSN_SIZEOF = 12

PCI_EXT_CAP_VNDR_HDR_SIZEOF = 8

# MSI-X registers
PCI_MSIX_FLAGS = 2  # Message Control
PCI_MSIX_TABLE = 4  # Table offset
PCI_MSIX_FLAGS_MASKALL = 0x4000  # Mask all vectors for this function
PCI_MSIX_FLAGS_ENABLE = 0x8000  # MSI-X enable
PCI_CAP_MSIX_SIZEOF = 12  # size of MSIX registers


# from linux/vfio.h

VFIO_DEVICE_FLAGS_RESET = (1 << 0)
VFIO_DEVICE_FLAGS_PCI = (1 << 1)

VFIO_REGION_INFO_FLAG_READ = (1 << 0)
VFIO_REGION_INFO_FLAG_WRITE = (1 << 1)
VFIO_REGION_INFO_FLAG_MMAP = (1 << 2)
VFIO_REGION_INFO_FLAG_CAPS = (1 << 3)

VFIO_REGION_TYPE_MIGRATION = 3
VFIO_REGION_SUBTYPE_MIGRATION = 1

VFIO_REGION_INFO_CAP_SPARSE_MMAP = 1
VFIO_REGION_INFO_CAP_TYPE = 2

VFIO_IRQ_INFO_EVENTFD = (1 << 0)

VFIO_IRQ_SET_DATA_NONE = (1 << 0)
VFIO_IRQ_SET_DATA_BOOL = (1 << 1)
VFIO_IRQ_SET_DATA_EVENTFD = (1 << 2)
VFIO_IRQ_SET_ACTION_MASK = (1 << 3)
VFIO_IRQ_SET_ACTION_UNMASK = (1 << 4)
VFIO_IRQ_SET_ACTION_TRIGGER = (1 << 5)

VFIO_DMA_UNMAP_FLAG_ALL = (1 << 1)

VFIO_DEVICE_STATE_V1_STOP = (0)
VFIO_DEVICE_STATE_V1_RUNNING = (1 << 0)
VFIO_DEVICE_STATE_V1_SAVING = (1 << 1)
VFIO_DEVICE_STATE_V1_RESUMING = (1 << 2)
VFIO_DEVICE_STATE_MASK = ((1 << 3) - 1)


# libvfio-user defines

VFU_TRANS_SOCK = 0
VFU_TRANS_PIPE = 1
VFU_TRANS_MAX = 2

LIBVFIO_USER_FLAG_ATTACH_NB = (1 << 0)
VFU_DEV_TYPE_PCI = 0

LIBVFIO_USER_MAJOR = 0
LIBVFIO_USER_MINOR = 1

VFIO_USER_CLIENT_MAX_FDS_LIMIT = 1024

SERVER_MAX_FDS = 8

ONE_TB = (1024 * 1024 * 1024 * 1024)

VFIO_USER_DEFAULT_MAX_DATA_XFER_SIZE = (1024 * 1024)
SERVER_MAX_DATA_XFER_SIZE = VFIO_USER_DEFAULT_MAX_DATA_XFER_SIZE
SERVER_MAX_MSG_SIZE = SERVER_MAX_DATA_XFER_SIZE + 16 + 16

MAX_DMA_REGIONS = 16
MAX_DMA_SIZE = (8 * ONE_TB)

# enum vfio_user_command
VFIO_USER_VERSION = 1
VFIO_USER_DMA_MAP = 2
VFIO_USER_DMA_UNMAP = 3
VFIO_USER_DEVICE_GET_INFO = 4
VFIO_USER_DEVICE_GET_REGION_INFO = 5
VFIO_USER_DEVICE_GET_REGION_IO_FDS = 6
VFIO_USER_DEVICE_GET_IRQ_INFO = 7
VFIO_USER_DEVICE_SET_IRQS = 8
VFIO_USER_REGION_READ = 9
VFIO_USER_REGION_WRITE = 10
VFIO_USER_DMA_READ = 11
VFIO_USER_DMA_WRITE = 12
VFIO_USER_DEVICE_RESET = 13
VFIO_USER_DIRTY_PAGES = 14
VFIO_USER_MAX = 15

VFIO_USER_F_TYPE_COMMAND = 0
VFIO_USER_F_TYPE_REPLY = 1

SIZEOF_VFIO_USER_HEADER = 16

VFU_PCI_DEV_BAR0_REGION_IDX = 0
VFU_PCI_DEV_BAR1_REGION_IDX = 1
VFU_PCI_DEV_BAR2_REGION_IDX = 2
VFU_PCI_DEV_BAR3_REGION_IDX = 3
VFU_PCI_DEV_BAR4_REGION_IDX = 4
VFU_PCI_DEV_BAR5_REGION_IDX = 5
VFU_PCI_DEV_ROM_REGION_IDX = 6
VFU_PCI_DEV_CFG_REGION_IDX = 7
VFU_PCI_DEV_VGA_REGION_IDX = 8
VFU_PCI_DEV_MIGR_REGION_IDX = 9
VFU_PCI_DEV_NUM_REGIONS = 10

VFU_REGION_FLAG_READ = 1
VFU_REGION_FLAG_WRITE = 2
VFU_REGION_FLAG_RW = (VFU_REGION_FLAG_READ | VFU_REGION_FLAG_WRITE)
VFU_REGION_FLAG_MEM = 4
VFU_REGION_FLAG_ALWAYS_CB = 8

VFIO_USER_F_DMA_REGION_READ = (1 << 0)
VFIO_USER_F_DMA_REGION_WRITE = (1 << 1)

VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP = (1 << 0)

VFIO_IOMMU_DIRTY_PAGES_FLAG_START = (1 << 0)
VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP = (1 << 1)
VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP = (1 << 2)

VFIO_USER_IO_FD_TYPE_IOEVENTFD = 0
VFIO_USER_IO_FD_TYPE_IOREGIONFD = 1


# enum vfu_dev_irq_type
VFU_DEV_INTX_IRQ = 0
VFU_DEV_MSI_IRQ = 1
VFU_DEV_MSIX_IRQ = 2
VFU_DEV_ERR_IRQ = 3
VFU_DEV_REQ_IRQ = 4
VFU_DEV_NUM_IRQS = 5

# enum vfu_reset_type
VFU_RESET_DEVICE = 0
VFU_RESET_LOST_CONN = 1
VFU_RESET_PCI_FLR = 2

# vfu_pci_type_t
VFU_PCI_TYPE_CONVENTIONAL = 0
VFU_PCI_TYPE_PCI_X_1 = 1
VFU_PCI_TYPE_PCI_X_2 = 2
VFU_PCI_TYPE_EXPRESS = 3

VFU_CAP_FLAG_EXTENDED = (1 << 0)
VFU_CAP_FLAG_CALLBACK = (1 << 1)
VFU_CAP_FLAG_READONLY = (1 << 2)

VFU_MIGR_CALLBACKS_VERS = 1

SOCK_PATH = b"/tmp/vfio-user.sock.%d" % os.getpid()

topdir = os.path.realpath(os.path.dirname(__file__) + "/../..")
libname = os.path.join(os.getenv("LIBVFIO_SO_DIR"), "libvfio-user.so")
lib = c.CDLL(libname, use_errno=True)
libc = c.CDLL("libc.so.6", use_errno=True)

#
# Structures
#


class Structure(c.Structure):
    def __len__(self):
        """Handy method to return length in bytes."""
        return len(bytes(self))

    @classmethod
    def pop_from_buffer(cls, buf):
        """"Pop a new object from the given bytes buffer."""
        obj = cls.from_buffer_copy(buf)
        return obj, buf[c.sizeof(obj):]


class vfu_bar_t(c.Union):
    _pack_ = 1
    _fields_ = [
        ("mem", c.c_int32),
        ("io", c.c_int32)
    ]


class vfu_pci_hdr_intr_t(Structure):
    _pack_ = 1
    _fields_ = [
        ("iline", c.c_byte),
        ("ipin", c.c_byte)
    ]


class vfu_pci_hdr_t(Structure):
    _pack_ = 1
    _fields_ = [
        ("id", c.c_int32),
        ("cmd", c.c_uint16),
        ("sts", c.c_uint16),
        ("rid", c.c_byte),
        ("cc_pi", c.c_byte),
        ("cc_scc", c.c_byte),
        ("cc_bcc", c.c_byte),
        ("cls", c.c_byte),
        ("mlt", c.c_byte),
        ("htype", c.c_byte),
        ("bist", c.c_byte),
        ("bars", vfu_bar_t * PCI_BARS_NR),
        ("ccptr", c.c_int32),
        ("ss", c.c_int32),
        ("erom", c.c_int32),
        ("cap", c.c_byte),
        ("res1", c.c_byte * 7),
        ("intr", vfu_pci_hdr_intr_t),
        ("mgnt", c.c_byte),
        ("mlat", c.c_byte)
    ]


class iovec_t(Structure):
    _fields_ = [
        ("iov_base", c.c_void_p),
        ("iov_len", c.c_int32)
    ]

    def __eq__(self, other):
        if type(self) != type(other):
            return False
        return self.iov_base == other.iov_base \
            and self.iov_len == other.iov_len

    def __str__(self):
        return "%s-%s" % \
            (hex(self.iov_base or 0), hex((self.iov_base or 0) + self.iov_len))

    def __copy__(self):
        cls = self.__class__
        result = cls.__new__(cls)
        result.iov_base = self.iov_base
        result.iov_len = self.iov_len
        return result


class vfio_irq_info(Structure):
    _pack_ = 1
    _fields_ = [
        ("argsz", c.c_uint32),
        ("flags", c.c_uint32),
        ("index", c.c_uint32),
        ("count", c.c_uint32),
    ]


class vfio_irq_set(Structure):
    _pack_ = 1
    _fields_ = [
        ("argsz", c.c_uint32),
        ("flags", c.c_uint32),
        ("index", c.c_uint32),
        ("start", c.c_uint32),
        ("count", c.c_uint32),
    ]


class vfio_user_device_info(Structure):
    _pack_ = 1
    _fields_ = [
        ("argsz", c.c_uint32),
        ("flags", c.c_uint32),
        ("num_regions", c.c_uint32),
        ("num_irqs", c.c_uint32),
    ]


class vfio_region_info(Structure):
    _pack_ = 1
    _fields_ = [
        ("argsz", c.c_uint32),
        ("flags", c.c_uint32),
        ("index", c.c_uint32),
        ("cap_offset", c.c_uint32),
        ("size", c.c_uint64),
        ("offset", c.c_uint64),
    ]


class vfio_region_info_cap_type(Structure):
    _pack_ = 1
    _fields_ = [
        ("id", c.c_uint16),
        ("version", c.c_uint16),
        ("next", c.c_uint32),
        ("type", c.c_uint32),
        ("subtype", c.c_uint32),
    ]


class vfio_region_info_cap_sparse_mmap(Structure):
    _pack_ = 1
    _fields_ = [
        ("id", c.c_uint16),
        ("version", c.c_uint16),
        ("next", c.c_uint32),
        ("nr_areas", c.c_uint32),
        ("reserved", c.c_uint32),
    ]


class vfio_region_sparse_mmap_area(Structure):
    _pack_ = 1
    _fields_ = [
        ("offset", c.c_uint64),
        ("size", c.c_uint64),
    ]


class vfio_user_region_io_fds_request(Structure):
    _pack_ = 1
    _fields_ = [
        ("argsz", c.c_uint32),
        ("flags", c.c_uint32),
        ("index", c.c_uint32),
        ("count", c.c_uint32)
    ]


class vfio_user_sub_region_ioeventfd(Structure):
    _pack_ = 1
    _fields_ = [
        ("offset", c.c_uint64),
        ("size", c.c_uint64),
        ("fd_index", c.c_uint32),
        ("type", c.c_uint32),
        ("flags", c.c_uint32),
        ("padding", c.c_uint32),
        ("datamatch", c.c_uint64)
    ]


class vfio_user_sub_region_ioregionfd(Structure):
    _pack_ = 1
    _fields_ = [
        ("offset", c.c_uint64),
        ("size", c.c_uint64),
        ("fd_index", c.c_uint32),
        ("type", c.c_uint32),
        ("flags", c.c_uint32),
        ("padding", c.c_uint32),
        ("user_data", c.c_uint64)
    ]


class vfio_user_sub_region_io_fd(c.Union):
    _pack_ = 1
    _fields_ = [
        ("sub_region_ioeventfd", vfio_user_sub_region_ioeventfd),
        ("sub_region_ioregionfd", vfio_user_sub_region_ioregionfd)
    ]


class vfio_user_region_io_fds_reply(Structure):
    _pack_ = 1
    _fields_ = [
        ("argsz", c.c_uint32),
        ("flags", c.c_uint32),
        ("index", c.c_uint32),
        ("count", c.c_uint32)
    ]


class vfio_user_dma_map(Structure):
    _pack_ = 1
    _fields_ = [
        ("argsz", c.c_uint32),
        ("flags", c.c_uint32),
        ("offset", c.c_uint64),
        ("addr", c.c_uint64),
        ("size", c.c_uint64),
    ]


class vfio_user_dma_unmap(Structure):
    _pack_ = 1
    _fields_ = [
        ("argsz", c.c_uint32),
        ("flags", c.c_uint32),
        ("addr", c.c_uint64),
        ("size", c.c_uint64),
    ]


class vfu_dma_info_t(Structure):
    _fields_ = [
        ("iova", iovec_t),
        ("vaddr", c.c_void_p),
        ("mapping", iovec_t),
        ("page_size", c.c_size_t),
        ("prot", c.c_uint32)
    ]

    def __eq__(self, other):
        if type(self) != type(other):
            return False
        return self.iova == other.iova \
            and self.vaddr == other.vaddr \
            and self.mapping == other.mapping \
            and self.page_size == other.page_size \
            and self.prot == other.prot

    def __str__(self):
        return "IOVA=%s vaddr=%s mapping=%s page_size=%s prot=%s" % \
            (self.iova, self.vaddr, self.mapping, hex(self.page_size),
            bin(self.prot))

    def __copy__(self):
        cls = self.__class__
        result = cls.__new__(cls)
        result.iova = self.iova
        result.vaddr = self.vaddr
        result.mapping = self.mapping
        result.page_size = self.page_size
        result.prot = self.prot
        return result


class vfio_user_dirty_pages(Structure):
    _pack_ = 1
    _fields_ = [
        ("argsz", c.c_uint32),
        ("flags", c.c_uint32)
    ]


class vfio_user_bitmap(Structure):
    _pack_ = 1
    _fields_ = [
        ("pgsize", c.c_uint64),
        ("size", c.c_uint64)
    ]


class vfio_user_bitmap_range(Structure):
    _pack_ = 1
    _fields_ = [
        ("iova", c.c_uint64),
        ("size", c.c_uint64),
        ("bitmap", vfio_user_bitmap)
    ]


transition_cb_t = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_int, use_errno=True)
get_pending_bytes_cb_t = c.CFUNCTYPE(c.c_uint64, c.c_void_p)
prepare_data_cb_t = c.CFUNCTYPE(c.c_void_p, c.POINTER(c.c_uint64),
                                c.POINTER(c.c_uint64))
read_data_cb_t = c.CFUNCTYPE(c.c_ssize_t, c.c_void_p, c.c_void_p,
                             c.c_uint64, c.c_uint64)
write_data_cb_t = c.CFUNCTYPE(c.c_ssize_t, c.c_void_p, c.c_uint64)
data_written_cb_t = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint64)


class vfu_migration_callbacks_t(Structure):
    _fields_ = [
        ("version", c.c_int),
        ("transition", transition_cb_t),
        ("get_pending_bytes", get_pending_bytes_cb_t),
        ("prepare_data", prepare_data_cb_t),
        ("read_data", read_data_cb_t),
        ("write_data", write_data_cb_t),
        ("data_written", data_written_cb_t),
    ]


class dma_sg_t(Structure):
    _fields_ = [
        ("dma_addr", c.c_void_p),
        ("region", c.c_int),
        ("length", c.c_uint64),
        ("offset", c.c_uint64),
        ("writeable", c.c_bool),
        ("le_next", c.c_void_p),
        ("le_prev", c.c_void_p),
    ]

    def __str__(self):
        return "DMA addr=%s, region index=%s, length=%s, offset=%s, RW=%s" % \
            (hex(self.dma_addr), self.region, hex(self.length),
                hex(self.offset), self.writeable)


class vfio_user_migration_info(Structure):
    _pack_ = 1
    _fields_ = [
        ("device_state", c.c_uint32),
        ("reserved", c.c_uint32),
        ("pending_bytes", c.c_uint64),
        ("data_offset", c.c_uint64),
        ("data_size", c.c_uint64),
    ]


#
# Util functions
#


lib.vfu_create_ctx.argtypes = (c.c_int, c.c_char_p, c.c_int,
                               c.c_void_p, c.c_int)
lib.vfu_create_ctx.restype = (c.c_void_p)
lib.vfu_setup_log.argtypes = (c.c_void_p, c.c_void_p, c.c_int)
lib.vfu_realize_ctx.argtypes = (c.c_void_p,)
lib.vfu_attach_ctx.argtypes = (c.c_void_p,)
lib.vfu_run_ctx.argtypes = (c.c_void_p,)
lib.vfu_destroy_ctx.argtypes = (c.c_void_p,)
vfu_region_access_cb_t = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(c.c_char),
                                     c.c_ulong, c.c_long, c.c_bool)
lib.vfu_setup_region.argtypes = (c.c_void_p, c.c_int, c.c_ulong,
                                 vfu_region_access_cb_t, c.c_int, c.c_void_p,
                                 c.c_uint32, c.c_int, c.c_ulong)
vfu_reset_cb_t = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_int)
lib.vfu_setup_device_reset_cb.argtypes = (c.c_void_p, vfu_reset_cb_t)
lib.vfu_pci_get_config_space.argtypes = (c.c_void_p,)
lib.vfu_pci_get_config_space.restype = (c.c_void_p)
lib.vfu_setup_device_nr_irqs.argtypes = (c.c_void_p, c.c_int, c.c_uint32)
lib.vfu_pci_init.argtypes = (c.c_void_p, c.c_int, c.c_int, c.c_int)
lib.vfu_pci_add_capability.argtypes = (c.c_void_p, c.c_ulong, c.c_int,
                                       c.POINTER(c.c_byte))
lib.vfu_pci_find_capability.argtypes = (c.c_void_p, c.c_bool, c.c_int)
lib.vfu_pci_find_capability.restype = (c.c_ulong)
lib.vfu_pci_find_next_capability.argtypes = (c.c_void_p, c.c_bool, c.c_ulong,
                                             c.c_int)
lib.vfu_pci_find_next_capability.restype = (c.c_ulong)
lib.vfu_irq_trigger.argtypes = (c.c_void_p, c.c_uint)
vfu_device_quiesce_cb_t = c.CFUNCTYPE(c.c_int, c.c_void_p, use_errno=True)
lib.vfu_setup_device_quiesce_cb.argtypes = (c.c_void_p,
                                            vfu_device_quiesce_cb_t)
vfu_dma_register_cb_t = c.CFUNCTYPE(None, c.c_void_p,
                                    c.POINTER(vfu_dma_info_t), use_errno=True)
vfu_dma_unregister_cb_t = c.CFUNCTYPE(None, c.c_void_p,
                                      c.POINTER(vfu_dma_info_t),
                                      use_errno=True)
lib.vfu_setup_device_dma.argtypes = (c.c_void_p, vfu_dma_register_cb_t,
                                     vfu_dma_unregister_cb_t)
lib.vfu_setup_device_migration_callbacks.argtypes = (c.c_void_p,
    c.POINTER(vfu_migration_callbacks_t), c.c_uint64)
lib.dma_sg_size.restype = (c.c_size_t)
lib.vfu_addr_to_sg.argtypes = (c.c_void_p, c.c_void_p, c.c_size_t,
                               c.POINTER(dma_sg_t), c.c_int, c.c_int)
lib.vfu_map_sg.argtypes = (c.c_void_p, c.POINTER(dma_sg_t), c.POINTER(iovec_t),
                           c.c_int, c.c_int)
lib.vfu_unmap_sg.argtypes = (c.c_void_p, c.POINTER(dma_sg_t),
                             c.POINTER(iovec_t), c.c_int)

lib.vfu_create_ioeventfd.argtypes = (c.c_void_p, c.c_uint32, c.c_int,
                                     c.c_size_t, c.c_uint32, c.c_uint32,
                                     c.c_uint64)

lib.vfu_device_quiesced.argtypes = (c.c_void_p, c.c_int)


def to_byte(val):
    """Cast an int to a byte value."""
    return val.to_bytes(1, 'little')


def skip(fmt, buf):
    """Return the data remaining after skipping the given elements."""
    return buf[struct.calcsize(fmt):]


def parse_json(json_str):
    """Parse JSON into an object with attributes (instead of using a dict)."""
    return json.loads(json_str, object_hook=lambda d: SimpleNamespace(**d))


def eventfd(initval=0, flags=0):
    libc.eventfd.argtypes = (c.c_uint, c.c_int)
    return libc.eventfd(initval, flags)


def connect_sock():
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(SOCK_PATH)
    return sock


def connect_client(ctx):
    sock = connect_sock()

    json = b'{ "capabilities": { "max_msg_fds": 8 } }'
    # struct vfio_user_version
    payload = struct.pack("HH%dsc" % len(json), LIBVFIO_USER_MAJOR,
                          LIBVFIO_USER_MINOR, json, b'\0')
    hdr = vfio_user_header(VFIO_USER_VERSION, size=len(payload))
    sock.send(hdr + payload)
    vfu_attach_ctx(ctx, expect=0)
    payload = get_reply(sock, expect=0)
    return sock


def disconnect_client(ctx, sock):
    sock.close()

    # notice client closed connection
    vfu_run_ctx(ctx, errno.ENOTCONN)


def get_reply(sock, expect=0):
    buf = sock.recv(4096)
    (msg_id, cmd, msg_size, flags, errno) = struct.unpack("HHIII", buf[0:16])
    assert (flags & VFIO_USER_F_TYPE_REPLY) != 0
    assert errno == expect
    return buf[16:]


def msg(ctx, sock, cmd, payload=bytearray(), expect=0, fds=None,
        rsp=True, busy=False):
    """
    Round trip a request and reply to the server. vfu_run_ctx will be
    called once for the server to process the incoming message,
    If a response is not expected then @rsp must be set to False, otherwise
    this function will block indefinitely.
    If busy is True, then we expect the server to have returned EBUSY from a
    quiesce callback, and hence vfu_run_ctx(); in this case, there will be no
    response: it can later be retrieved, post vfu_device_quiesced(), with
    get_reply().
    """
    hdr = vfio_user_header(cmd, size=len(payload))

    if fds:
        sock.sendmsg([hdr + payload], [(socket.SOL_SOCKET, socket.SCM_RIGHTS,
                                        struct.pack("I" * len(fds), *fds))])
    else:
        sock.send(hdr + payload)

    if busy:
        vfu_run_ctx(ctx, errno.EBUSY)
        rsp = False
    else:
        vfu_run_ctx(ctx)

    if not rsp:
        return
    return get_reply(sock, expect=expect)


def get_reply_fds(sock, expect=0):
    """Receives a message from a socket and pulls the returned file descriptors
       out of the message."""
    fds = array.array("i")
    data, ancillary, flags, addr = sock.recvmsg(4096,
                                            socket.CMSG_LEN(64 * fds.itemsize))
    (msg_id, cmd, msg_size, msg_flags, errno) = struct.unpack("HHIII",
                                                              data[0:16])
    assert errno == expect

    cmsg_level, cmsg_type, packed_fd = ancillary[0] if len(ancillary) != 0 \
                                                    else (0, 0, [])
    unpacked_fds = []
    for i in range(0, len(packed_fd), 4):
        [unpacked_fd] = struct.unpack_from("i", packed_fd, offset=i)
        unpacked_fds.append(unpacked_fd)
    assert len(packed_fd)/4 == len(unpacked_fds)
    assert (msg_flags & VFIO_USER_F_TYPE_REPLY) != 0
    return (unpacked_fds, data[16:])


def msg_fds(ctx, sock, cmd, payload, expect=0, fds=None):
    """Round trip a request and reply to the server. With the server returning
       new fds"""
    hdr = vfio_user_header(cmd, size=len(payload))

    if fds:
        sock.sendmsg([hdr + payload], [(socket.SOL_SOCKET, socket.SCM_RIGHTS,
                                        struct.pack("I" * len(fds), *fds))])
    else:
        sock.send(hdr + payload)

    vfu_run_ctx(ctx)
    return get_reply_fds(sock, expect=expect)


def get_pci_header(ctx):
    ptr = lib.vfu_pci_get_config_space(ctx)
    return c.cast(ptr, c.POINTER(vfu_pci_hdr_t)).contents


def get_pci_cfg_space(ctx):
    ptr = lib.vfu_pci_get_config_space(ctx)
    return c.cast(ptr, c.POINTER(c.c_char))[0:PCI_CFG_SPACE_SIZE]


def get_pci_ext_cfg_space(ctx):
    ptr = lib.vfu_pci_get_config_space(ctx)
    return c.cast(ptr, c.POINTER(c.c_char))[0:PCI_CFG_SPACE_EXP_SIZE]


def read_pci_cfg_space(ctx, buf, count, offset, extended=False):
    space = get_pci_ext_cfg_space(ctx) if extended else get_pci_cfg_space(ctx)

    for i in range(count):
        buf[i] = space[offset+i]
    return count


def write_pci_cfg_space(ctx, buf, count, offset, extended=False):
    max_offset = PCI_CFG_SPACE_EXP_SIZE if extended else PCI_CFG_SPACE_SIZE

    assert offset + count <= max_offset

    space = c.cast(lib.vfu_pci_get_config_space(ctx), c.POINTER(c.c_char))

    for i in range(count):
        space[offset+i] = buf[i]
    return count


def access_region(ctx, sock, is_write, region, offset, count,
                  data=None, expect=0, rsp=True, busy=False):
    # struct vfio_user_region_access
    payload = struct.pack("QII", offset, region, count)
    if is_write:
        payload += data

    cmd = VFIO_USER_REGION_WRITE if is_write else VFIO_USER_REGION_READ

    result = msg(ctx, sock, cmd, payload, expect=expect, rsp=rsp, busy=busy)

    if is_write:
        return None

    if rsp:
        return skip("QII", result)


def write_region(ctx, sock, region, offset, count, data, expect=0, rsp=True,
                 busy=False):
    access_region(ctx, sock, True, region, offset, count, data, expect=expect,
                  rsp=rsp, busy=busy)


def read_region(ctx, sock, region, offset, count, expect=0, rsp=True,
                busy=False):
    return access_region(ctx, sock, False, region, offset, count,
        expect=expect, rsp=rsp, busy=busy)


def ext_cap_hdr(buf, offset):
    """Read an extended cap header."""

    # struct pcie_ext_cap_hdr
    cap_id, cap_next = struct.unpack_from('HH', buf, offset)
    cap_next >>= 4
    return cap_id, cap_next


def dma_register(ctx, info):
    pass


@vfu_dma_register_cb_t
def __dma_register(ctx, info):
    # The copy is required because in case of deliberate failure (e.g.
    # test_dma_map_busy_reply_fail) the memory gets deallocated and mock only
    # records the pointer, so the contents are all null/zero.
    dma_register(ctx, copy.copy(info.contents))


def dma_unregister(ctx, info):
    pass


@vfu_dma_unregister_cb_t
def __dma_unregister(ctx, info):
    dma_unregister(ctx, copy.copy(info.contents))


def quiesce_cb(ctx):
    return 0


@vfu_device_quiesce_cb_t
def _quiesce_cb(ctx):
    return quiesce_cb(ctx)


def vfu_setup_device_quiesce_cb(ctx, quiesce_cb=_quiesce_cb):
    assert ctx is not None
    lib.vfu_setup_device_quiesce_cb(ctx,
                                       c.cast(quiesce_cb,
                                              vfu_device_quiesce_cb_t))


def reset_cb(ctx, reset_type):
    return 0


@vfu_reset_cb_t
def _reset_cb(ctx, reset_type):
    return reset_cb(ctx, reset_type)


def vfu_setup_device_reset_cb(ctx, cb=_reset_cb):
    assert ctx is not None
    return lib.vfu_setup_device_reset_cb(ctx, c.cast(cb, vfu_reset_cb_t))


def prepare_ctx_for_dma(dma_register=__dma_register,
                        dma_unregister=__dma_unregister, quiesce=_quiesce_cb,
                        reset=_reset_cb, migration_callbacks=False):
    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_pci_init(ctx)
    assert ret == 0

    ret = vfu_setup_device_dma(ctx, dma_register, dma_unregister)
    assert ret == 0

    if quiesce is not None:
        vfu_setup_device_quiesce_cb(ctx, quiesce)

    if reset is not None:
        ret = vfu_setup_device_reset_cb(ctx, reset)
        assert ret == 0

    f = tempfile.TemporaryFile()
    f.truncate(0x2000)

    mmap_areas = [(0x1000, 0x1000)]

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_MIGR_REGION_IDX, size=0x2000,
                           flags=VFU_REGION_FLAG_RW, mmap_areas=mmap_areas,
                           fd=f.fileno())
    assert ret == 0

    if migration_callbacks:
        ret = vfu_setup_device_migration_callbacks(ctx)
        assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    return ctx

#
# Library wrappers
#


msg_id = 1


@c.CFUNCTYPE(None, c.c_void_p, c.c_int, c.c_char_p)
def log(ctx, level, msg):
    lvl2str = {syslog.LOG_EMERG: "EMERGENCY",
                syslog.LOG_ALERT: "ALERT",
                syslog.LOG_CRIT: "CRITICAL",
                syslog.LOG_ERR: "ERROR",
                syslog.LOG_WARNING: "WANRING",
                syslog.LOG_NOTICE: "NOTICE",
                syslog.LOG_INFO: "INFO",
                syslog.LOG_DEBUG: "DEBUG"}
    print(lvl2str[level] + ": " + msg.decode("utf-8"))


def vfio_user_header(cmd, size, no_reply=False, error=False, error_no=0):
    global msg_id

    buf = struct.pack("HHIII", msg_id, cmd, SIZEOF_VFIO_USER_HEADER + size,
                      VFIO_USER_F_TYPE_COMMAND, error_no)

    msg_id += 1

    return buf


def vfu_create_ctx(trans=VFU_TRANS_SOCK, sock_path=SOCK_PATH, flags=0,
                   private=None, dev_type=VFU_DEV_TYPE_PCI):
    if os.path.exists(sock_path):
        os.remove(sock_path)

    ctx = lib.vfu_create_ctx(trans, sock_path, flags, private, dev_type)

    if ctx:
        lib.vfu_setup_log(ctx, log, syslog.LOG_DEBUG)

    return ctx


def vfu_realize_ctx(ctx):
    return lib.vfu_realize_ctx(ctx)


def vfu_attach_ctx(ctx, expect=0):
    ret = lib.vfu_attach_ctx(ctx)
    if expect == 0:
        assert ret == 0, "failed to attach: %s" % os.strerror(c.get_errno())
    else:
        assert ret == -1
        assert c.get_errno() == expect
    return ret


def vfu_run_ctx(ctx, expect=0):
    ret = lib.vfu_run_ctx(ctx)
    if expect == 0:
        assert ret >= 0, "vfu_run_ctx(): %s" % os.strerror(c.get_errno())
    else:
        assert ret == -1
        assert c.get_errno() == expect
    return ret


def vfu_destroy_ctx(ctx):
    lib.vfu_destroy_ctx(ctx)
    ctx = None
    if os.path.exists(SOCK_PATH):
        os.remove(SOCK_PATH)


def pci_region_cb(ctx, buf, count, offset, is_write):
    pass


@vfu_region_access_cb_t
def __pci_region_cb(ctx, buf, count, offset, is_write):
    return pci_region_cb(ctx, buf, count, offset, is_write)


def vfu_setup_region(ctx, index, size, cb=__pci_region_cb, flags=0,
                     mmap_areas=None, nr_mmap_areas=None, fd=-1, offset=0):
    assert ctx is not None

    c_mmap_areas = None

    if mmap_areas:
        c_mmap_areas = (iovec_t * len(mmap_areas))(*mmap_areas)

    if nr_mmap_areas is None:
        if mmap_areas:
            nr_mmap_areas = len(mmap_areas)
        else:
            nr_mmap_areas = 0

    # We're sending a file descriptor to ourselves; to pretend the server is
    # separate, we need to dup() here.
    if fd != -1:
        fd = os.dup(fd)

    ret = lib.vfu_setup_region(ctx, index, size,
                               c.cast(cb, vfu_region_access_cb_t),
                               flags, c_mmap_areas, nr_mmap_areas, fd, offset)

    if fd != -1 and ret != 0:
        os.close(fd)

    return ret


def vfu_setup_device_nr_irqs(ctx, irqtype, count):
    assert ctx is not None
    return lib.vfu_setup_device_nr_irqs(ctx, irqtype, count)


def vfu_pci_init(ctx, pci_type=VFU_PCI_TYPE_EXPRESS,
                 hdr_type=PCI_HEADER_TYPE_NORMAL):
    assert ctx is not None
    return lib.vfu_pci_init(ctx, pci_type, hdr_type, 0)


def vfu_pci_add_capability(ctx, pos, flags, data):
    assert ctx is not None

    databuf = (c.c_byte * len(data)).from_buffer(bytearray(data))
    return lib.vfu_pci_add_capability(ctx, pos, flags, databuf)


def vfu_pci_find_capability(ctx, extended, cap_id):
    assert ctx is not None

    return lib.vfu_pci_find_capability(ctx, extended, cap_id)


def vfu_pci_find_next_capability(ctx, extended, offset, cap_id):
    assert ctx is not None

    return lib.vfu_pci_find_next_capability(ctx, extended, offset, cap_id)


def vfu_irq_trigger(ctx, subindex):
    assert ctx is not None

    return lib.vfu_irq_trigger(ctx, subindex)


def vfu_setup_device_dma(ctx, register_cb=None, unregister_cb=None):
    assert ctx is not None

    return lib.vfu_setup_device_dma(ctx, c.cast(register_cb,
                                                vfu_dma_register_cb_t),
                                         c.cast(unregister_cb,
                                                vfu_dma_unregister_cb_t))


# FIXME some of the migration arguments are probably wrong as in the C version
# they're pointer. Check how we handle the read/write region callbacks.

def migr_trans_cb(ctx, state):
    pass


@transition_cb_t
def __migr_trans_cb(ctx, state):
    return migr_trans_cb(ctx, state)


def migr_get_pending_bytes_cb(ctx):
    pass


@get_pending_bytes_cb_t
def __migr_get_pending_bytes_cb(ctx):
    return migr_get_pending_bytes_cb(ctx)


def migr_prepare_data_cb(ctx, offset, size):
    pass


@prepare_data_cb_t
def __migr_prepare_data_cb(ctx, offset, size):
    return migr_prepare_data_cb(ctx, offset, size)


def migr_read_data_cb(ctx, buf, count, offset):
    pass


@read_data_cb_t
def __migr_read_data_cb(ctx, buf, count, offset):
    return migr_read_data_cb(ctx, buf, count, offset)


def migr_write_data_cb(ctx, buf, count, offset):
    pass


@write_data_cb_t
def __migr_write_data_cb(ctx, buf, count, offset):
    return migr_write_data_cb(ctx, buf, count, offset)


def migr_data_written_cb(ctx, count):
    pass


@data_written_cb_t
def __migr_data_written_cb(ctx, count):
    return migr_data_written_cb(ctx, count)


def vfu_setup_device_migration_callbacks(ctx, cbs=None, offset=0x4000):
    assert ctx is not None

    if not cbs:
        cbs = vfu_migration_callbacks_t()
        cbs.version = VFU_MIGR_CALLBACKS_VERS
        cbs.transition = __migr_trans_cb
        cbs.get_pending_bytes = __migr_get_pending_bytes_cb
        cbs.prepare_data = __migr_prepare_data_cb
        cbs.read_data = __migr_read_data_cb
        cbs.write_data = __migr_write_data_cb
        cbs.data_written = __migr_data_written_cb

    return lib.vfu_setup_device_migration_callbacks(ctx, cbs, offset)


def dma_sg_size():
    return lib.dma_sg_size()


def vfu_addr_to_sg(ctx, dma_addr, length, max_sg=1,
                   prot=(mmap.PROT_READ | mmap.PROT_WRITE)):
    assert ctx is not None

    sg = (dma_sg_t * max_sg)()

    return (lib.vfu_addr_to_sg(ctx, dma_addr, length, sg, max_sg, prot), sg)


def vfu_map_sg(ctx, sg, iovec, cnt=1, flags=0):
    return lib.vfu_map_sg(ctx, sg, iovec, cnt, flags)


def vfu_unmap_sg(ctx, sg, iovec, cnt=1):
    return lib.vfu_unmap_sg(ctx, sg, iovec, cnt)


def vfu_create_ioeventfd(ctx, region_idx, fd, offset, size, flags, datamatch):
    assert ctx is not None

    return lib.vfu_create_ioeventfd(ctx, region_idx, fd, offset, size,
                                    flags, datamatch)


def vfu_device_quiesced(ctx, err):
    return lib.vfu_device_quiesced(ctx, err)


def fail_with_errno(err):
    def side_effect(args, *kwargs):
        c.set_errno(err)
        return -1
    return side_effect


# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
