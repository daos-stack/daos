#
# Copyright(c) 2022 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause
#

from ctypes import c_int

from pyocf.types.cache import Cache
from pyocf.types.data import Data
from pyocf.types.core import Core
from pyocf.types.io import IoDir
from pyocf.types.volume import Volume, IoFlags
from pyocf.utils import Size
from pyocf.types.shared import OcfCompletion


def test_large_flush(pyocf_ctx):
    cache_device = Volume(Size.from_MiB(50))
    core_device = Volume(Size.from_MiB(100))

    cache = Cache.start_on_device(cache_device)
    core = Core.using_device(core_device)
    cache.add_core(core)
    queue = cache.get_default_queue()

    io = core.new_io(queue, 0, core_device.size.bytes, IoDir.WRITE, 0, IoFlags.FLUSH)
    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    data = Data(byte_count=0)
    io.set_data(data, 0)
    io.submit_flush()
    completion.wait()

    assert int(completion.results["err"]) == 0

    cache.stop()


def test_large_discard(pyocf_ctx):
    cache_device = Volume(Size.from_MiB(50))
    core_device = Volume(Size.from_MiB(100))

    cache = Cache.start_on_device(cache_device)
    core = Core.using_device(core_device)
    cache.add_core(core)
    queue = cache.get_default_queue()

    io = core.new_io(queue, 0, core_device.size.bytes, IoDir.WRITE, 0, 0)
    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    data = Data(byte_count=0)
    io.set_data(data, 0)
    io.submit_discard()
    completion.wait()

    assert int(completion.results["err"]) == 0

    cache.stop()


def test_large_io(pyocf_ctx):
    cache_device = Volume(Size.from_MiB(50))
    core_device = Volume(Size.from_MiB(100))

    cache = Cache.start_on_device(cache_device)
    core = Core.using_device(core_device)
    cache.add_core(core)
    queue = cache.get_default_queue()

    io = core.new_io(queue, 0, core_device.size.bytes, IoDir.WRITE, 0, 0)
    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    data = Data(byte_count=core_device.size.bytes)
    io.set_data(data)
    io.submit()
    completion.wait()

    assert int(completion.results["err"]) == 0

    cache.stop()
