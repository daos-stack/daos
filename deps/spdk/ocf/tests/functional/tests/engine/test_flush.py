#
# Copyright(c) 2022-2022 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
from ctypes import c_int

from pyocf.types.cache import Cache
from pyocf.types.data import Data
from pyocf.types.core import Core
from pyocf.types.io import IoDir
from pyocf.types.volume import Volume, IoFlags, TraceDevice
from pyocf.utils import Size
from pyocf.types.shared import OcfCompletion


def test_flush_propagation(pyocf_ctx):
    flushes = {}

    pyocf_ctx.register_volume_type(TraceDevice)

    def trace_flush(vol, io, io_type):
        nonlocal flushes

        if io_type == TraceDevice.IoType.Flush:
            if vol.uuid not in flushes:
                flushes[vol.uuid] = []
            flushes[vol.uuid].append((io.contents._addr, io.contents._bytes))

        return True

    cache_device = TraceDevice(Size.from_MiB(50), trace_fcn=trace_flush)
    core_device = TraceDevice(Size.from_MiB(100), trace_fcn=trace_flush)

    addr = Size.from_MiB(2).B
    size = Size.from_MiB(1).B

    cache = Cache.start_on_device(cache_device)
    core = Core.using_device(core_device)
    cache.add_core(core)
    queue = cache.get_default_queue()

    flushes = {}

    io = core.new_io(queue, addr, size, IoDir.WRITE, 0, IoFlags.FLUSH)
    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    data = Data(byte_count=0)
    io.set_data(data, 0)

    io.submit_flush()
    completion.wait()

    assert int(completion.results["err"]) == 0

    assert cache_device.uuid in flushes
    assert core_device.uuid in flushes

    cache_flushes = flushes[cache_device.uuid]
    core_flushes = flushes[core_device.uuid]

    assert len(cache_flushes) == 1
    assert len(core_flushes) == 1

    assert core_flushes[0] == (addr, size)

    # empty flush expected to be sent to cache device
    assert cache_flushes[0] == (0, 0)

    cache.stop()
