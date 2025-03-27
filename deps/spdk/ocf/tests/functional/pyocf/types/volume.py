#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import (
    POINTER,
    c_void_p,
    c_uint32,
    c_char_p,
    create_string_buffer,
    memmove,
    memset,
    Structure,
    CFUNCTYPE,
    c_int,
    c_uint,
    c_uint64,
    sizeof,
    cast,
    string_at,
)
from hashlib import md5
import weakref
from enum import IntEnum

from .io import Io, IoOps, IoDir
from .shared import OcfErrorCode, Uuid
from ..ocf import OcfLib
from ..utils import print_buffer, Size as S
from .data import Data


class IoFlags(IntEnum):
    FLUSH = 1


class VolumeCaps(Structure):
    _fields_ = [("_atomic_writes", c_uint32, 1)]


class VolumeOps(Structure):
    SUBMIT_IO = CFUNCTYPE(None, POINTER(Io))
    SUBMIT_FLUSH = CFUNCTYPE(None, c_void_p)
    SUBMIT_METADATA = CFUNCTYPE(None, c_void_p)
    SUBMIT_DISCARD = CFUNCTYPE(None, c_void_p)
    SUBMIT_WRITE_ZEROES = CFUNCTYPE(None, c_void_p)
    OPEN = CFUNCTYPE(c_int, c_void_p)
    CLOSE = CFUNCTYPE(None, c_void_p)
    GET_MAX_IO_SIZE = CFUNCTYPE(c_uint, c_void_p)
    GET_LENGTH = CFUNCTYPE(c_uint64, c_void_p)

    _fields_ = [
        ("_submit_io", SUBMIT_IO),
        ("_submit_flush", SUBMIT_FLUSH),
        ("_submit_metadata", SUBMIT_METADATA),
        ("_submit_discard", SUBMIT_DISCARD),
        ("_submit_write_zeroes", SUBMIT_WRITE_ZEROES),
        ("_open", OPEN),
        ("_close", CLOSE),
        ("_get_length", GET_LENGTH),
        ("_get_max_io_size", GET_MAX_IO_SIZE),
    ]


class VolumeProperties(Structure):
    _fields_ = [
        ("_name", c_char_p),
        ("_io_priv_size", c_uint32),
        ("_volume_priv_size", c_uint32),
        ("_caps", VolumeCaps),
        ("_io_ops", IoOps),
        ("_deinit", c_char_p),
        ("_ops", VolumeOps),
    ]


class VolumeIoPriv(Structure):
    _fields_ = [("_data", c_void_p), ("_offset", c_uint64)]


class Volume(Structure):
    VOLUME_POISON = 0x13

    _fields_ = [("_storage", c_void_p)]
    _instances_ = {}
    _uuid_ = {}

    props = None

    def __init__(self, size: S, uuid=None):
        super().__init__()
        self.size = size
        if uuid:
            if uuid in type(self)._uuid_:
                raise Exception(
                    "Volume with uuid {} already created".format(uuid)
                )
            self.uuid = uuid
        else:
            self.uuid = str(id(self))

        type(self)._uuid_[self.uuid] = weakref.ref(self)

        self.data = create_string_buffer(int(self.size))
        memset(self.data, self.VOLUME_POISON, self.size)
        self._storage = cast(self.data, c_void_p)

        self.reset_stats()
        self.opened = False

    def get_copy(self):
        new_volume = Volume(self.size)
        memmove(new_volume.data, self.data, self.size)
        return new_volume

    @classmethod
    def get_props(cls):
        if not cls.props:
            cls.props = VolumeProperties(
                _name=str(cls.__name__).encode("ascii"),
                _io_priv_size=sizeof(VolumeIoPriv),
                _volume_priv_size=0,
                _caps=VolumeCaps(_atomic_writes=0),
                _ops=VolumeOps(
                    _submit_io=cls._submit_io,
                    _submit_flush=cls._submit_flush,
                    _submit_metadata=cls._submit_metadata,
                    _submit_discard=cls._submit_discard,
                    _submit_write_zeroes=cls._submit_write_zeroes,
                    _open=cls._open,
                    _close=cls._close,
                    _get_max_io_size=cls._get_max_io_size,
                    _get_length=cls._get_length,
                ),
                _io_ops=IoOps(
                    _set_data=cls._io_set_data, _get_data=cls._io_get_data
                ),
                _deinit=0,
            )

        return cls.props

    @classmethod
    def get_instance(cls, ref):
        instance = cls._instances_[ref]()
        if instance is None:
            print("tried to access {} but it's gone".format(ref))

        return instance

    @classmethod
    def get_by_uuid(cls, uuid):
        return cls._uuid_[uuid]()

    @staticmethod
    @VolumeOps.SUBMIT_IO
    def _submit_io(io):
        io_structure = cast(io, POINTER(Io))
        volume = Volume.get_instance(
            OcfLib.getInstance().ocf_io_get_volume(io_structure)
        )

        volume.submit_io(io_structure)

    @staticmethod
    @VolumeOps.SUBMIT_FLUSH
    def _submit_flush(flush):
        io_structure = cast(flush, POINTER(Io))
        volume = Volume.get_instance(
            OcfLib.getInstance().ocf_io_get_volume(io_structure)
        )

        volume.submit_flush(io_structure)

    @staticmethod
    @VolumeOps.SUBMIT_METADATA
    def _submit_metadata(meta):
        pass

    @staticmethod
    @VolumeOps.SUBMIT_DISCARD
    def _submit_discard(discard):
        io_structure = cast(discard, POINTER(Io))
        volume = Volume.get_instance(
            OcfLib.getInstance().ocf_io_get_volume(io_structure)
        )

        volume.submit_discard(io_structure)

    @staticmethod
    @VolumeOps.SUBMIT_WRITE_ZEROES
    def _submit_write_zeroes(write_zeroes):
        pass

    @staticmethod
    @CFUNCTYPE(c_int, c_void_p)
    def _open(ref):
        uuid_ptr = cast(
            OcfLib.getInstance().ocf_volume_get_uuid(ref), POINTER(Uuid)
        )
        uuid = str(uuid_ptr.contents._data, encoding="ascii")
        try:
            volume = Volume.get_by_uuid(uuid)
        except:  # noqa E722 TODO:Investigate whether this really should be so broad
            print("Tried to access unallocated volume {}".format(uuid))
            print("{}".format(Volume._uuid_))
            return -1

        if volume.opened:
            return OcfErrorCode.OCF_ERR_NOT_OPEN_EXC

        Volume._instances_[ref] = weakref.ref(volume)

        return volume.open()

    @staticmethod
    @VolumeOps.CLOSE
    def _close(ref):
        volume = Volume.get_instance(ref)
        volume.close()
        volume.opened = False

    @staticmethod
    @VolumeOps.GET_MAX_IO_SIZE
    def _get_max_io_size(ref):
        return Volume.get_instance(ref).get_max_io_size()

    @staticmethod
    @VolumeOps.GET_LENGTH
    def _get_length(ref):
        return Volume.get_instance(ref).get_length()

    @staticmethod
    @IoOps.SET_DATA
    def _io_set_data(io, data, offset):
        io_priv = cast(
            OcfLib.getInstance().ocf_io_get_priv(io), POINTER(VolumeIoPriv)
        )
        data = Data.get_instance(data)
        io_priv.contents._offset = offset
        io_priv.contents._data = data.handle

        return 0

    @staticmethod
    @IoOps.GET_DATA
    def _io_get_data(io):
        io_priv = cast(
            OcfLib.getInstance().ocf_io_get_priv(io), POINTER(VolumeIoPriv)
        )
        return io_priv.contents._data

    def open(self):
        self.opened = True
        return 0

    def close(self):
        pass

    def get_length(self):
        return self.size

    def get_max_io_size(self):
        return S.from_KiB(128)

    def submit_flush(self, flush):
        flush.contents._end(flush, 0)

    def submit_discard(self, discard):
        try:
            dst = self._storage + discard.contents._addr
            memset(dst, 0, discard.contents._bytes)

            discard.contents._end(discard, 0)
        except:  # noqa E722
            discard.contents._end(discard, -5)

    def get_stats(self):
        return self.stats

    def reset_stats(self):
        self.stats = {IoDir.WRITE: 0, IoDir.READ: 0}

    def submit_io(self, io):
        flags = int(io.contents._flags)
        if flags & IoFlags.FLUSH:
            self.submit_flush(io)
            return

        try:
            self.stats[IoDir(io.contents._dir)] += 1

            io_priv = cast(
                OcfLib.getInstance().ocf_io_get_priv(io), POINTER(VolumeIoPriv))
            offset = io_priv.contents._offset

            if io.contents._dir == IoDir.WRITE:
                src_ptr = cast(OcfLib.getInstance().ocf_io_get_data(io), c_void_p)
                src = Data.get_instance(src_ptr.value).handle.value + offset
                dst = self._storage + io.contents._addr
            elif io.contents._dir == IoDir.READ:
                dst_ptr = cast(OcfLib.getInstance().ocf_io_get_data(io), c_void_p)
                dst = Data.get_instance(dst_ptr.value).handle.value + offset
                src = self._storage + io.contents._addr

            memmove(dst, src, io.contents._bytes)
            io_priv.contents._offset += io.contents._bytes

            io.contents._end(io, 0)
        except:  # noqa E722
            io.contents._end(io, -5)

    def dump(self, offset=0, size=0, ignore=VOLUME_POISON, **kwargs):
        if size == 0:
            size = int(self.size) - int(offset)

        print_buffer(
            self._storage,
            size,
            ignore=ignore,
            **kwargs
        )

    def md5(self):
        m = md5()
        m.update(string_at(self._storage, self.size))
        return m.hexdigest()


class ErrorDevice(Volume):
    def __init__(self, size, error_sectors: set = None, uuid=None):
        super().__init__(size, uuid)
        self.error_sectors = error_sectors or set()

    def set_mapping(self, error_sectors: set):
        self.error_sectors = error_sectors

    def submit_io(self, io):
        if io.contents._addr in self.error_sectors:
            io.contents._end(io, -5)
            self.stats["errors"][io.contents._dir] += 1
        else:
            super().submit_io(io)

    def reset_stats(self):
        super().reset_stats()
        self.stats["errors"] = {IoDir.WRITE: 0, IoDir.READ: 0}


class TraceDevice(Volume):
    class IoType(IntEnum):
        Data = 1
        Flush = 2
        Discard = 3

    def __init__(self, size, trace_fcn=None, uuid=None):
        super().__init__(size, uuid)
        self.trace_fcn = trace_fcn

    def _trace(self, io, io_type):
        submit = True

        if self.trace_fcn:
            submit = self.trace_fcn(self, io, io_type)

        return submit

    def submit_io(self, io):
        submit = self._trace(io, TraceDevice.IoType.Data)

        if submit:
            super().submit_io(io)

    def submit_flush(self, io):
        submit = self._trace(io, TraceDevice.IoType.Flush)

        if submit:
            super().submit_flush(io)


lib = OcfLib.getInstance()
lib.ocf_io_get_priv.restype = POINTER(VolumeIoPriv)
lib.ocf_io_get_volume.argtypes = [c_void_p]
lib.ocf_io_get_volume.restype = c_void_p
lib.ocf_io_get_data.argtypes = [c_void_p]
lib.ocf_io_get_data.restype = c_void_p
