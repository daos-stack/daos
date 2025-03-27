#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import logging
from ctypes import (
    c_size_t,
    c_void_p,
    Structure,
    c_int,
    c_uint8,
    c_uint16,
    c_uint32,
    c_uint64,
    c_char,
    c_char_p,
    c_bool,
    cast,
    byref,
    create_string_buffer,
)
from datetime import timedelta

from .data import Data
from .io import Io, IoDir
from .queue import Queue
from .shared import Uuid, OcfCompletion, OcfError, SeqCutOffPolicy
from .stats.core import CoreInfo
from .stats.shared import UsageStats, RequestsStats, BlocksStats, ErrorsStats
from .volume import Volume
from ..ocf import OcfLib
from ..utils import Size, struct_to_dict


class UserMetadata(Structure):
    _fields_ = [("data", c_void_p), ("size", c_size_t)]


class CoreConfig(Structure):
    MAX_CORE_NAME_SIZE = 32
    _fields_ = [
        ("_name", c_char * MAX_CORE_NAME_SIZE),
        ("_uuid", Uuid),
        ("_volume_type", c_uint8),
        ("_try_add", c_bool),
        ("_seq_cutoff_threshold", c_uint32),
        ("_seq_cutoff_promotion_count", c_uint32),
        ("_user_metadata", UserMetadata),
    ]


class Core:
    DEFAULT_ID = 4096
    DEFAULT_SEQ_CUTOFF_THRESHOLD = 1024 * 1024
    DEFAULT_SEQ_CUTOFF_PROMOTION_COUNT = 8

    def __init__(
        self,
        device: Volume,
        try_add: bool,
        name: str = "core",
        seq_cutoff_threshold: int = DEFAULT_SEQ_CUTOFF_THRESHOLD,
        seq_cutoff_promotion_count: int = DEFAULT_SEQ_CUTOFF_PROMOTION_COUNT,
    ):
        self.cache = None
        self.device = device
        self.device_name = device.uuid
        self.handle = c_void_p()
        self.cfg = CoreConfig(
            _uuid=Uuid(
                _data=cast(
                    create_string_buffer(self.device_name.encode("ascii")),
                    c_char_p,
                ),
                _size=len(self.device_name) + 1,
            ),
            _name=name.encode("ascii"),
            _volume_type=self.device.type_id,
            _try_add=try_add,
            _seq_cutoff_threshold=seq_cutoff_threshold,
            _seq_cutoff_promotion_count=seq_cutoff_promotion_count,
            _user_metadata=UserMetadata(_data=None, _size=0),
        )

    @classmethod
    def using_device(cls, device, **kwargs):
        c = cls(device=device, try_add=False, **kwargs)

        return c

    def get_cfg(self):
        return self.cfg

    def get_handle(self):
        return self.handle

    def new_io(
        self, queue: Queue, addr: int, length: int, direction: IoDir,
        io_class: int, flags: int
    ):
        if not self.cache:
            raise Exception("Core isn't attached to any cache")

        io = OcfLib.getInstance().ocf_core_new_io_wrapper(
            self.handle, queue.handle, addr, length, direction, io_class, flags)

        if io is None:
            raise Exception("Failed to create io!")

        return Io.from_pointer(io)

    def new_core_io(
        self, queue: Queue, addr: int, length: int, direction: IoDir,
        io_class: int, flags: int
    ):
        lib = OcfLib.getInstance()
        volume = lib.ocf_core_get_volume(self.handle)
        io = lib.ocf_volume_new_io(
            volume, queue.handle, addr, length, direction, io_class, flags)
        return Io.from_pointer(io)

    def get_stats(self):
        core_info = CoreInfo()
        usage = UsageStats()
        req = RequestsStats()
        blocks = BlocksStats()
        errors = ErrorsStats()

        self.cache.read_lock()
        status = self.cache.owner.lib.ocf_stats_collect_core(
            self.handle, byref(usage), byref(req), byref(blocks), byref(errors)
        )
        if status:
            self.cache.read_unlock()
            raise OcfError("Failed collecting core stats", status)

        status = self.cache.owner.lib.ocf_core_get_info(
            self.handle, byref(core_info)
        )
        if status:
            self.cache.read_unlock()
            raise OcfError("Failed getting core stats", status)

        self.cache.read_unlock()
        return {
            "size": Size(core_info.core_size_bytes),
            "dirty_for": timedelta(seconds=core_info.dirty_for),
            "seq_cutoff_policy": SeqCutOffPolicy(core_info.seq_cutoff_policy),
            "seq_cutoff_threshold": core_info.seq_cutoff_threshold,
            "usage": struct_to_dict(usage),
            "req": struct_to_dict(req),
            "blocks": struct_to_dict(blocks),
            "errors": struct_to_dict(errors),
        }

    def set_seq_cut_off_policy(self, policy: SeqCutOffPolicy):
        self.cache.write_lock()

        status = self.cache.owner.lib.ocf_mngt_core_set_seq_cutoff_policy(
            self.handle, policy
        )
        self.cache.write_unlock()
        if status:
            raise OcfError("Error setting core seq cut off policy", status)

    def set_seq_cut_off_threshold(self, threshold):
        self.cache.write_lock()

        status = self.cache.owner.lib.ocf_mngt_core_set_seq_cutoff_threshold(
            self.handle, threshold
        )
        self.cache.write_unlock()
        if status:
            raise OcfError("Error setting core seq cut off policy threshold", status)

    def set_seq_cut_off_promotion(self, count):
        self.cache.write_lock()

        status = self.cache.owner.lib.ocf_mngt_core_set_seq_cutoff_promotion_count(
            self.handle, count
        )
        self.cache.write_unlock()
        if status:
            raise OcfError("Error setting core seq cut off policy promotion count", status)

    def reset_stats(self):
        self.cache.owner.lib.ocf_core_stats_initialize(self.handle)

    def exp_obj_md5(self):
        logging.getLogger("pyocf").warning(
            "Reading whole exported object! This disturbs statistics values"
        )

        cache_line_size = int(self.cache.get_stats()['conf']['cache_line_size'])
        read_buffer_all = Data(self.device.size)

        read_buffer = Data(cache_line_size)

        position = 0
        while position < read_buffer_all.size:
            io = self.new_io(self.cache.get_default_queue(), position,
                             cache_line_size, IoDir.READ, 0, 0)
            io.set_data(read_buffer)

            cmpl = OcfCompletion([("err", c_int)])
            io.callback = cmpl.callback
            io.submit()
            cmpl.wait()

            if cmpl.results["err"]:
                raise Exception("Error reading whole exported object")

            read_buffer_all.copy(read_buffer, position, 0, cache_line_size)
            position += cache_line_size

        return read_buffer_all.md5()


lib = OcfLib.getInstance()
lib.ocf_core_get_volume.restype = c_void_p
lib.ocf_volume_new_io.argtypes = [
    c_void_p,
    c_void_p,
    c_uint64,
    c_uint32,
    c_uint32,
    c_uint32,
    c_uint64,
]
lib.ocf_volume_new_io.restype = c_void_p
lib.ocf_core_get_volume.argtypes = [c_void_p]
lib.ocf_core_get_volume.restype = c_void_p
lib.ocf_mngt_core_set_seq_cutoff_policy.argtypes = [c_void_p, c_uint32]
lib.ocf_mngt_core_set_seq_cutoff_policy.restype = c_int
lib.ocf_mngt_core_set_seq_cutoff_threshold.argtypes = [c_void_p, c_uint32]
lib.ocf_mngt_core_set_seq_cutoff_threshold.restype = c_int
lib.ocf_mngt_core_set_seq_cutoff_promotion_count.argtypes = [c_void_p, c_uint32]
lib.ocf_mngt_core_set_seq_cutoff_promotion_count.restype = c_int
lib.ocf_stats_collect_core.argtypes = [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p]
lib.ocf_stats_collect_core.restype = c_int
lib.ocf_core_get_info.argtypes = [c_void_p, c_void_p]
lib.ocf_core_get_info.restype = c_int
lib.ocf_core_new_io_wrapper.argtypes = [
    c_void_p,
    c_void_p,
    c_uint64,
    c_uint32,
    c_uint32,
    c_uint32,
    c_uint64,
]
lib.ocf_core_new_io_wrapper.restype = c_void_p
