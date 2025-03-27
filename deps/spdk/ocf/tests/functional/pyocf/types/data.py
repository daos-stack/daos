#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import (
    c_void_p,
    c_uint32,
    CFUNCTYPE,
    c_uint64,
    create_string_buffer,
    cast,
    memset,
    string_at,
    Structure,
    c_int,
    memmove,
    byref,
)
from enum import IntEnum
from hashlib import md5
import weakref

from ..utils import print_buffer, Size as S


class DataSeek(IntEnum):
    BEGIN = 0
    CURRENT = 1


class DataOps(Structure):
    ALLOC = CFUNCTYPE(c_void_p, c_uint32)
    FREE = CFUNCTYPE(None, c_void_p)
    MLOCK = CFUNCTYPE(c_int, c_void_p)
    MUNLOCK = CFUNCTYPE(None, c_void_p)
    READ = CFUNCTYPE(c_uint32, c_void_p, c_void_p, c_uint32)
    WRITE = CFUNCTYPE(c_uint32, c_void_p, c_void_p, c_uint32)
    ZERO = CFUNCTYPE(c_uint32, c_void_p, c_uint32)
    SEEK = CFUNCTYPE(c_uint32, c_void_p, c_uint32, c_uint32)
    COPY = CFUNCTYPE(c_uint64, c_void_p, c_void_p, c_uint64, c_uint64, c_uint64)
    SECURE_ERASE = CFUNCTYPE(None, c_void_p)

    _fields_ = [
        ("_alloc", ALLOC),
        ("_free", FREE),
        ("_mlock", MLOCK),
        ("_munlock", MUNLOCK),
        ("_read", READ),
        ("_write", WRITE),
        ("_zero", ZERO),
        ("_seek", SEEK),
        ("_copy", COPY),
        ("_secure_erase", SECURE_ERASE),
    ]


class Data:
    DATA_POISON = 0xA5
    PAGE_SIZE = 4096

    _instances_ = {}
    _ocf_instances_ = []

    def __init__(self, byte_count: int):
        self.size = int(byte_count)
        self.position = 0
        self.buffer = create_string_buffer(int(self.size))
        self.handle = cast(byref(self.buffer), c_void_p)

        memset(self.handle, self.DATA_POISON, self.size)
        type(self)._instances_[self.handle.value] = weakref.ref(self)
        self._as_parameter_ = self.handle

    @classmethod
    def get_instance(cls, ref):
        return cls._instances_[ref]()

    @classmethod
    def get_ops(cls):
        return DataOps(
            _alloc=cls._alloc,
            _free=cls._free,
            _mlock=cls._mlock,
            _munlock=cls._munlock,
            _read=cls._read,
            _write=cls._write,
            _zero=cls._zero,
            _seek=cls._seek,
            _copy=cls._copy,
            _secure_erase=cls._secure_erase,
        )

    @classmethod
    def pages(cls, pages: int):
        return cls(pages * Data.PAGE_SIZE)

    @classmethod
    def from_bytes(cls, source: bytes, offset: int = 0, size: int = 0):
        if size == 0:
            size = len(source) - offset
        d = cls(size)

        memmove(d.handle, cast(source, c_void_p).value + offset, size)

        return d

    @classmethod
    def from_string(cls, source: str, encoding: str = "ascii"):
        b = bytes(source, encoding)
        # duplicate string to fill space up to sector boundary
        padding_len = S.from_B(len(b), sector_aligned=True).B - len(b)
        padding = b * (padding_len // len(b) + 1)
        padding = padding[:padding_len]
        b = b + padding
        return cls.from_bytes(b)

    @staticmethod
    @DataOps.ALLOC
    def _alloc(pages):
        data = Data.pages(pages)
        Data._ocf_instances_.append(data)

        return data.handle.value

    @staticmethod
    @DataOps.FREE
    def _free(ref):
        Data._ocf_instances_.remove(Data.get_instance(ref))

    @staticmethod
    @DataOps.MLOCK
    def _mlock(ref):
        return Data.get_instance(ref).mlock()

    @staticmethod
    @DataOps.MUNLOCK
    def _munlock(ref):
        Data.get_instance(ref).munlock()

    @staticmethod
    @DataOps.READ
    def _read(dst, src, size):
        return Data.get_instance(src).read(dst, size)

    @staticmethod
    @DataOps.WRITE
    def _write(dst, src, size):
        return Data.get_instance(dst).write(src, size)

    @staticmethod
    @DataOps.ZERO
    def _zero(dst, size):
        return Data.get_instance(dst).zero(size)

    @staticmethod
    @DataOps.SEEK
    def _seek(dst, seek, size):
        return Data.get_instance(dst).seek(DataSeek(seek), size)

    @staticmethod
    @DataOps.COPY
    def _copy(dst, src, skip, seek, size):
        return Data.get_instance(dst).copy(
            Data.get_instance(src), skip, seek, size
        )

    @staticmethod
    @DataOps.SECURE_ERASE
    def _secure_erase(dst):
        Data.get_instance(dst).secure_erase()

    def read(self, dst, size):
        to_read = min(self.size - self.position, size)
        memmove(dst, self.handle.value + self.position, to_read)

        self.position += to_read
        return to_read

    def write(self, src, size):
        to_write = min(self.size - self.position, size)
        memmove(self.handle.value + self.position, src, to_write)

        self.position += to_write
        return to_write

    def mlock(self):
        return 0

    def munlock(self):
        pass

    def zero(self, size):
        to_zero = min(self.size - self.position, size)
        memset(self.handle.value + self.position, 0, to_zero)

        self.position += to_zero
        return to_zero

    def seek(self, seek, size):
        if seek == DataSeek.CURRENT:
            to_move = min(self.size - self.position, size)
            self.position += to_move
        else:
            to_move = min(self.size, size)
            self.position = to_move

        return to_move

    def copy(self, src, skip, seek, size):
        to_write = min(self.size - skip, size, src.size - seek)

        memmove(self.handle.value + skip, src.handle.value + seek, to_write)
        return to_write

    def secure_erase(self):
        pass

    def dump(self, ignore=DATA_POISON, **kwargs):
        print_buffer(self.buffer, self.size, ignore=ignore, **kwargs)

    def md5(self):
        m = md5()
        m.update(string_at(self.handle, self.size))
        return m.hexdigest()
