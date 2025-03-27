# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import pytest
from ctypes import c_int

from random import randint
from pyocf.types.cache import Cache, CacheMode
from pyocf.types.core import Core
from pyocf.types.volume import Volume
from pyocf.types.data import Data
from pyocf.types.io import IoDir
from pyocf.utils import Size as S
from pyocf.types.shared import OcfError, OcfCompletion, CacheLineSize


@pytest.mark.parametrize("cache_mode", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_adding_core(pyocf_ctx, cache_mode, cls):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=cache_mode, cache_line_size=cls
    )

    # Create core device
    core_device = Volume(S.from_MiB(10))
    core = Core.using_device(core_device)

    # Check statistics before adding core
    stats = cache.get_stats()
    assert stats["conf"]["core_count"] == 0

    # Add core to cache
    cache.add_core(core)

    # Check statistics after adding core
    stats = cache.get_stats()
    assert stats["conf"]["core_count"] == 1


@pytest.mark.parametrize("cache_mode", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_removing_core(pyocf_ctx, cache_mode, cls):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=cache_mode, cache_line_size=cls
    )

    # Create core device
    core_device = Volume(S.from_MiB(10))
    core = Core.using_device(core_device)

    # Add core to cache
    cache.add_core(core)

    # Remove core from cache
    cache.remove_core(core)

    # Check statistics after removing core
    stats = cache.get_stats()
    assert stats["conf"]["core_count"] == 0


@pytest.mark.parametrize("cache_mode", [CacheMode.WB])
@pytest.mark.parametrize("cls", CacheLineSize)
def test_remove_dirty_no_flush(pyocf_ctx, cache_mode, cls):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=cache_mode, cache_line_size=cls
    )

    # Create core device
    core_device = Volume(S.from_MiB(10))
    core = Core.using_device(core_device)
    cache.add_core(core)

    # Prepare data
    core_size = core.get_stats()["size"]
    data = Data(core_size.B)

    _io_to_core(core, data)

    # Remove core from cache
    cache.remove_core(core)


def test_30add_remove(pyocf_ctx):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device)

    # Create core device
    core_device = Volume(S.from_MiB(10))
    core = Core.using_device(core_device)

    # Add and remove core device in a loop 100 times
    # Check statistics after every operation
    for i in range(0, 30):
        cache.add_core(core)
        stats = cache.get_stats()
        assert stats["conf"]["core_count"] == 1

        cache.remove_core(core)
        stats = cache.get_stats()
        assert stats["conf"]["core_count"] == 0


def test_10add_remove_with_io(pyocf_ctx):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device)

    # Create core device
    core_device = Volume(S.from_MiB(10))
    core = Core.using_device(core_device)

    # Add and remove core 10 times in a loop with io in between
    for i in range(0, 10):
        cache.add_core(core)
        stats = cache.get_stats()
        assert stats["conf"]["core_count"] == 1

        write_data = Data.from_string("Test data")
        io = core.new_io(
            cache.get_default_queue(), S.from_sector(1).B, write_data.size,
            IoDir.WRITE, 0, 0
        )
        io.set_data(write_data)

        cmpl = OcfCompletion([("err", c_int)])
        io.callback = cmpl.callback
        io.submit()
        cmpl.wait()

        cache.remove_core(core)
        stats = cache.get_stats()
        assert stats["conf"]["core_count"] == 0


def test_add_remove_30core(pyocf_ctx):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device)
    core_devices = []
    core_amount = 30

    # Add 50 cores and check stats after each addition
    for i in range(0, core_amount):
        stats = cache.get_stats()
        assert stats["conf"]["core_count"] == i
        core_device = Volume(S.from_MiB(10))
        core = Core.using_device(core_device, name=f"core{i}")
        core_devices.append(core)
        cache.add_core(core)

    # Remove 50 cores and check stats before each removal
    for i in range(0, core_amount):
        stats = cache.get_stats()
        assert stats["conf"]["core_count"] == core_amount - i
        cache.remove_core(core_devices[i])

    # Check statistics
    stats = cache.get_stats()
    assert stats["conf"]["core_count"] == 0


def test_adding_to_random_cache(pyocf_ctx):
    cache_devices = []
    core_devices = {}
    cache_amount = 5
    core_amount = 30

    # Create 5 cache devices
    for i in range(0, cache_amount):
        cache_device = Volume(S.from_MiB(30))
        cache = Cache.start_on_device(cache_device, name=f"cache{i}")
        cache_devices.append(cache)

    # Create 50 core devices and add to random cache
    for i in range(0, core_amount):
        core_device = Volume(S.from_MiB(10))
        core = Core.using_device(core_device, name=f"core{i}")
        core_devices[core] = randint(0, cache_amount - 1)
        cache_devices[core_devices[core]].add_core(core)

    # Count expected number of cores per cache
    count_dict = {}
    for i in range(0, cache_amount):
        count_dict[i] = sum(k == i for k in core_devices.values())

    # Check if cache statistics are as expected
    for i in range(0, cache_amount):
        stats = cache_devices[i].get_stats()
        assert stats["conf"]["core_count"] == count_dict[i]


@pytest.mark.parametrize("cache_mode", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_adding_core_twice(pyocf_ctx, cache_mode, cls):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=cache_mode, cache_line_size=cls
    )

    # Create core device
    core_device = Volume(S.from_MiB(10))
    core = Core.using_device(core_device)

    # Add core
    cache.add_core(core)

    # Check that it is not possible to add the same core again
    with pytest.raises(OcfError):
        cache.add_core(core)

    # Check that core count is still equal to one
    stats = cache.get_stats()
    assert stats["conf"]["core_count"] == 1


@pytest.mark.parametrize("cache_mode", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_adding_core_already_used(pyocf_ctx, cache_mode, cls):
    # Start first cache device
    cache_device1 = Volume(S.from_MiB(30))
    cache1 = Cache.start_on_device(
        cache_device1, cache_mode=cache_mode, cache_line_size=cls, name="cache1"
    )

    # Start second cache device
    cache_device2 = Volume(S.from_MiB(30))
    cache2 = Cache.start_on_device(
        cache_device2, cache_mode=cache_mode, cache_line_size=cls, name="cache2"
    )

    # Create core device
    core_device = Volume(S.from_MiB(10))
    core = Core.using_device(core_device)

    # Add core to first cache
    cache1.add_core(core)

    # Check that it is not possible to add core to second cache
    with pytest.raises(OcfError):
        cache2.add_core(core)

    # Check that core count is as expected
    stats = cache1.get_stats()
    assert stats["conf"]["core_count"] == 1

    stats = cache2.get_stats()
    assert stats["conf"]["core_count"] == 0


@pytest.mark.parametrize("cache_mode", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_add_remove_incrementally(pyocf_ctx, cache_mode, cls):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=cache_mode, cache_line_size=cls
    )
    core_devices = []
    core_amount = 5

    # Create 5 core devices and add to cache
    for i in range(0, core_amount):
        core_device = Volume(S.from_MiB(10))
        core = Core.using_device(core_device, name=f"core{i}")
        core_devices.append(core)
        cache.add_core(core)

    # Check that core count is as expected
    stats = cache.get_stats()
    assert stats["conf"]["core_count"] == core_amount

    # Remove 3 cores
    cache.remove_core(core_devices[0])
    cache.remove_core(core_devices[1])
    cache.remove_core(core_devices[2])

    # Add 2 cores and check if core count is as expected
    cache.add_core(core_devices[0])
    cache.add_core(core_devices[1])
    stats = cache.get_stats()
    assert stats["conf"]["core_count"] == core_amount - 1

    # Remove 1 core and check if core count is as expected
    cache.remove_core(core_devices[1])
    stats = cache.get_stats()
    assert stats["conf"]["core_count"] == core_amount - 2

    # Add 2 cores and check if core count is as expected
    cache.add_core(core_devices[1])
    cache.add_core(core_devices[2])
    stats = cache.get_stats()
    assert stats["conf"]["core_count"] == core_amount


def _io_to_core(exported_obj: Core, data: Data):
    io = exported_obj.new_io(exported_obj.cache.get_default_queue(), 0, data.size,
            IoDir.WRITE, 0, 0)
    io.set_data(data)

    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    io.submit()
    completion.wait()

    assert completion.results["err"] == 0, "IO to exported object completion"
