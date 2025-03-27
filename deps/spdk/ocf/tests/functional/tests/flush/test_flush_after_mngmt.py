#
# Copyright(c) 2020-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import c_int, memmove, cast, c_void_p
from enum import IntEnum
from itertools import product
import random

import pytest

from pyocf.types.cache import Cache, CacheMode
from pyocf.types.core import Core
from pyocf.types.volume import Volume
from pyocf.types.data import Data
from pyocf.types.io import IoDir
from pyocf.utils import Size
from pyocf.types.shared import OcfCompletion


def __io(io, queue, address, size, data, direction):
    io.set_data(data, 0)
    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    io.submit()
    completion.wait()
    return int(completion.results["err"])


def _io(new_io, queue, address, size, data, offset, direction, flags):
    io = new_io(queue, address, size, direction, 0, flags)
    if direction == IoDir.READ:
        _data = Data.from_bytes(bytes(size))
    else:
        _data = Data.from_bytes(data, offset, size)
    ret = __io(io, queue, address, size, _data, direction)
    if not ret and direction == IoDir.READ:
        memmove(cast(data, c_void_p).value + offset, _data.handle, size)
    return ret


def io_to_exp_obj(core, address, size, data, offset, direction, flags):
    return _io(
        core.new_io,
        core.cache.get_default_queue(),
        address,
        size,
        data,
        offset,
        direction,
        flags,
    )


class FlushValVolume(Volume):
    def __init__(self, size):
        self.flush_last = False
        super().__init__(size)

    def submit_io(self, io):
        self.flush_last = False
        super().submit_io(io)

    def submit_flush(self, flush):
        self.flush_last = True
        super().submit_flush(flush)


def test_flush_after_mngmt(pyocf_ctx):
    """
    Check whether underlying volumes volatile caches (VC) are flushed after management operation
    """
    block_size = 4096

    data = bytes(block_size)

    cache_device = FlushValVolume(Size.from_MiB(30))
    core_device = FlushValVolume(Size.from_MiB(30))

    # after start cache VC must be cleared
    cache = Cache.start_on_device(cache_device, cache_mode=CacheMode.WT)
    assert cache_device.flush_last

    # adding core must flush VC
    core = Core.using_device(core_device)
    cache.add_core(core)
    assert cache_device.flush_last

    # WT I/O to write data to core and cache VC
    io_to_exp_obj(core, block_size * 0, block_size, data, 0, IoDir.WRITE, 0)

    # WB I/O to produce dirty cachelines in CAS
    cache.change_cache_mode(CacheMode.WB)
    io_to_exp_obj(core, block_size * 1, block_size, data, 0, IoDir.WRITE, 0)

    # after cache flush VCs are expected to be cleared
    cache.flush()
    assert cache_device.flush_last
    assert core_device.flush_last

    # I/O to write data to cache device VC
    io_to_exp_obj(core, block_size * 0, block_size, data, 0, IoDir.WRITE, 0)

    # cache save must flush VC
    cache.save()
    assert cache_device.flush_last

    # I/O to write data to cache device VC
    io_to_exp_obj(core, block_size * 0, block_size, data, 0, IoDir.WRITE, 0)

    # cache stop must flush VC
    cache.stop()
    assert cache_device.flush_last
