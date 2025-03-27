#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import logging
from ctypes import c_int, c_void_p, byref, c_uint32
from random import randrange
from itertools import count

import pytest

from pyocf.ocf import OcfLib
from pyocf.types.cache import Cache, CacheMode, MetadataLayout,  CleaningPolicy
from pyocf.types.core import Core
from pyocf.types.data import Data
from pyocf.types.io import IoDir
from pyocf.types.shared import OcfError, OcfCompletion, CacheLineSize, SeqCutOffPolicy
from pyocf.types.volume import Volume
from pyocf.utils import Size

logger = logging.getLogger(__name__)


def test_start_check_default(pyocf_ctx):
    """Test if default values are correct after start.
    """

    cache_device = Volume(Size.from_MiB(40))
    core_device = Volume(Size.from_MiB(10))
    cache = Cache.start_on_device(cache_device)

    core = Core.using_device(core_device)
    cache.add_core(core)

    # Check if values are default
    stats = cache.get_stats()
    assert stats["conf"]["cleaning_policy"] == CleaningPolicy.DEFAULT
    assert stats["conf"]["cache_mode"] == CacheMode.DEFAULT
    assert stats["conf"]["cache_line_size"] == CacheLineSize.DEFAULT

    core_stats = core.get_stats()
    assert core_stats["seq_cutoff_policy"] == SeqCutOffPolicy.DEFAULT


@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.parametrize("mode", CacheMode)
def test_start_write_first_and_check_mode(pyocf_ctx, mode: CacheMode, cls: CacheLineSize):
    """Test starting cache in different modes with different cache line sizes.
    After start check proper cache mode behaviour, starting with write operation.
    """

    cache_device = Volume(Size.from_MiB(40))
    core_device = Volume(Size.from_MiB(10))
    cache = Cache.start_on_device(cache_device, cache_mode=mode, cache_line_size=cls)
    core_exported = Core.using_device(core_device)

    cache.add_core(core_exported)

    logger.info("[STAGE] Initial write to exported object")
    cache_device.reset_stats()
    core_device.reset_stats()

    test_data = Data.from_string("This is test data")
    io_to_core(core_exported, test_data, Size.from_sector(1).B)
    check_stats_write_empty(core_exported, mode, cls)

    logger.info("[STAGE] Read from exported object after initial write")
    io_from_exported_object(core_exported, test_data.size, Size.from_sector(1).B)
    check_stats_read_after_write(core_exported, mode, cls, True)

    logger.info("[STAGE] Write to exported object after read")
    cache_device.reset_stats()
    core_device.reset_stats()

    test_data = Data.from_string("Changed test data")

    io_to_core(core_exported, test_data, Size.from_sector(1).B)
    check_stats_write_after_read(core_exported, mode, cls)

    check_md5_sums(core_exported, mode)


@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.parametrize("mode", CacheMode)
def test_start_read_first_and_check_mode(pyocf_ctx, mode: CacheMode, cls: CacheLineSize):
    """Starting cache in different modes with different cache line sizes.
    After start check proper cache mode behaviour, starting with read operation.
    """

    cache_device = Volume(Size.from_MiB(20))
    core_device = Volume(Size.from_MiB(5))
    cache = Cache.start_on_device(cache_device, cache_mode=mode, cache_line_size=cls)
    core_exported = Core.using_device(core_device)

    cache.add_core(core_exported)

    logger.info("[STAGE] Initial write to core device")
    test_data = Data.from_string("This is test data")
    io_to_core(core_exported, test_data, Size.from_sector(1).B, True)

    cache_device.reset_stats()
    core_device.reset_stats()

    logger.info("[STAGE] Initial read from exported object")
    io_from_exported_object(core_exported, test_data.size, Size.from_sector(1).B)
    check_stats_read_empty(core_exported, mode, cls)

    logger.info("[STAGE] Write to exported object after initial read")
    cache_device.reset_stats()
    core_device.reset_stats()

    test_data = Data.from_string("Changed test data")

    io_to_core(core_exported, test_data, Size.from_sector(1).B)

    check_stats_write_after_read(core_exported, mode, cls, True)

    logger.info("[STAGE] Read from exported object after write")
    io_from_exported_object(core_exported, test_data.size, Size.from_sector(1).B)
    check_stats_read_after_write(core_exported, mode, cls)

    check_md5_sums(core_exported, mode)


@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.parametrize("mode", CacheMode)
@pytest.mark.parametrize("layout", MetadataLayout)
def test_start_params(pyocf_ctx, mode: CacheMode, cls: CacheLineSize, layout: MetadataLayout):
    """Starting cache with different parameters.
    Check if cache starts without errors.
    If possible check whether cache reports properly set parameters.
    """
    cache_device = Volume(Size.from_MiB(20))
    queue_size = randrange(60000, 2**32)
    unblock_size = randrange(1, queue_size)
    volatile_metadata = randrange(2) == 1
    unaligned_io = randrange(2) == 1
    submit_fast = randrange(2) == 1
    name = "test"

    logger.info("[STAGE] Start cache")
    cache = Cache.start_on_device(
        cache_device,
        cache_mode=mode,
        cache_line_size=cls,
        name=name,
        metadata_layout=MetadataLayout.SEQUENTIAL,
        metadata_volatile=volatile_metadata,
        max_queue_size=queue_size,
        queue_unblock_size=unblock_size,
        pt_unaligned_io=unaligned_io,
        use_submit_fast=submit_fast)

    stats = cache.get_stats()
    assert stats["conf"]["cache_mode"] == mode, "Cache mode"
    assert stats["conf"]["cache_line_size"] == cls, "Cache line size"
    assert cache.get_name() == name, "Cache name"
    # TODO: metadata_layout, metadata_volatile, max_queue_size,
    #  queue_unblock_size, pt_unaligned_io, use_submit_fast
    # TODO: test in functional tests


@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.parametrize("mode", CacheMode)
@pytest.mark.parametrize("with_flush", {True, False})
def test_stop(pyocf_ctx, mode: CacheMode, cls: CacheLineSize, with_flush: bool):
    """Stopping cache.
    Check if cache is stopped properly in different modes with or without preceding flush operation.
    """

    cache_device = Volume(Size.from_MiB(20))
    core_device = Volume(Size.from_MiB(5))
    cache = Cache.start_on_device(cache_device, cache_mode=mode, cache_line_size=cls)
    core_exported = Core.using_device(core_device)
    cache.add_core(core_exported)
    cls_no = 10

    run_io_and_cache_data_if_possible(core_exported, mode, cls, cls_no)

    stats = cache.get_stats()
    assert int(stats["conf"]["dirty"]) == (cls_no if mode.lazy_write() else 0),\
        "Dirty data before MD5"

    md5_exported_core = core_exported.exp_obj_md5()

    if with_flush:
        cache.flush()
    cache.stop()

    if mode.lazy_write() and not with_flush:
        assert core_device.md5() != md5_exported_core, \
            "MD5 check: core device vs exported object with dirty data"
    else:
        assert core_device.md5() == md5_exported_core, \
            "MD5 check: core device vs exported object with clean data"


def test_start_stop_multiple(pyocf_ctx):
    """Starting/stopping multiple caches.
    Check whether OCF allows for starting multiple caches and stopping them in random order
    """

    caches = []
    caches_no = randrange(6, 11)
    for i in range(1, caches_no):
        cache_device = Volume(Size.from_MiB(20))
        cache_name = f"cache{i}"
        cache_mode = CacheMode(randrange(0, len(CacheMode)))
        size = 4096 * 2**randrange(0, len(CacheLineSize))
        cache_line_size = CacheLineSize(size)

        cache = Cache.start_on_device(
            cache_device,
            name=cache_name,
            cache_mode=cache_mode,
            cache_line_size=cache_line_size)
        caches.append(cache)
        stats = cache.get_stats()
        assert stats["conf"]["cache_mode"] == cache_mode, "Cache mode"
        assert stats["conf"]["cache_line_size"] == cache_line_size, "Cache line size"
        assert stats["conf"]["cache_name"] == cache_name, "Cache name"

    caches.sort(key=lambda e: randrange(1000))
    for cache in caches:
        logger.info("Getting stats before stopping cache")
        stats = cache.get_stats()
        cache_name = stats["conf"]["cache_name"]
        cache.stop()
        assert get_cache_by_name(pyocf_ctx, cache_name) != 0, "Try getting cache after stopping it"


def test_100_start_stop(pyocf_ctx):
    """Starting/stopping stress test.
    Check OCF behaviour when cache is started and stopped continuously
    """

    for i in range(1, 101):
        cache_device = Volume(Size.from_MiB(20))
        cache_name = f"cache{i}"
        cache_mode = CacheMode(randrange(0, len(CacheMode)))
        size = 4096 * 2**randrange(0, len(CacheLineSize))
        cache_line_size = CacheLineSize(size)

        cache = Cache.start_on_device(
            cache_device,
            name=cache_name,
            cache_mode=cache_mode,
            cache_line_size=cache_line_size)
        stats = cache.get_stats()
        assert stats["conf"]["cache_mode"] == cache_mode, "Cache mode"
        assert stats["conf"]["cache_line_size"] == cache_line_size, "Cache line size"
        assert stats["conf"]["cache_name"] == cache_name, "Cache name"
        cache.stop()
        assert get_cache_by_name(pyocf_ctx, "cache1") != 0, "Try getting cache after stopping it"


def test_start_stop_incrementally(pyocf_ctx):
    """Starting/stopping multiple caches incrementally.
    Check whether OCF behaves correctly when few caches at a time are
    in turns added and removed (#added > #removed) until their number reaches limit,
    and then proportions are reversed and number of caches gradually falls to 0.
    """

    counter = count()
    caches = []
    caches_limit = 10
    add = True
    run = True
    increase = True
    while run:
        if add:
            for i in range(0, randrange(3, 5) if increase else randrange(1, 3)):
                cache_device = Volume(Size.from_MiB(20))
                cache_name = f"cache{next(counter)}"
                cache_mode = CacheMode(randrange(0, len(CacheMode)))
                size = 4096 * 2**randrange(0, len(CacheLineSize))
                cache_line_size = CacheLineSize(size)

                cache = Cache.start_on_device(
                    cache_device,
                    name=cache_name,
                    cache_mode=cache_mode,
                    cache_line_size=cache_line_size)
                caches.append(cache)
                stats = cache.get_stats()
                assert stats["conf"]["cache_mode"] == cache_mode, "Cache mode"
                assert stats["conf"]["cache_line_size"] == cache_line_size, "Cache line size"
                assert stats["conf"]["cache_name"] == cache_name, "Cache name"
                if len(caches) == caches_limit:
                    increase = False
        else:
            for i in range(0, randrange(1, 3) if increase else randrange(3, 5)):
                if len(caches) == 0:
                    run = False
                    break
                cache = caches.pop()
                logger.info("Getting stats before stopping cache")
                stats = cache.get_stats()
                cache_name = stats["conf"]["cache_name"]
                cache.stop()
                assert get_cache_by_name(pyocf_ctx, cache_name) != 0, \
                    "Try getting cache after stopping it"
        add = not add


@pytest.mark.parametrize("mode", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_start_cache_same_id(pyocf_ctx, mode, cls):
    """Adding two caches with the same name
    Check that OCF does not allow for 2 caches to be started with the same cache_name
    """

    cache_device1 = Volume(Size.from_MiB(20))
    cache_device2 = Volume(Size.from_MiB(20))
    cache_name = "cache"
    cache = Cache.start_on_device(cache_device1,
                                  cache_mode=mode,
                                  cache_line_size=cls,
                                  name=cache_name)
    cache.get_stats()

    with pytest.raises(OcfError, match="OCF_ERR_CACHE_EXIST"):
        cache = Cache.start_on_device(cache_device2,
                                      cache_mode=mode,
                                      cache_line_size=cls,
                                      name=cache_name)
    cache.get_stats()


@pytest.mark.parametrize("cls", CacheLineSize)
def test_start_cache_huge_device(pyocf_ctx_log_buffer, cls):
    """
    Test whether we can start cache which would overflow ocf_cache_line_t type.
    pass_criteria:
      - Starting cache on device too big to handle should fail
    """
    class HugeDevice(Volume):
        def get_length(self):
            return Size.from_B((cls * c_uint32(-1).value))

        def submit_io(self, io):
            io.contents._end(io, 0)

    cache_device = HugeDevice(Size.from_MiB(20))

    with pytest.raises(OcfError, match="OCF_ERR_INVAL_CACHE_DEV"):
        cache = Cache.start_on_device(cache_device, cache_line_size=cls, metadata_volatile=True)

    assert any(
        [line.find("exceeds maximum") > 0 for line in pyocf_ctx_log_buffer.get_lines()]
    ), "Expected to find log notifying that max size was exceeded"



@pytest.mark.parametrize("mode", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_start_cache_same_device(pyocf_ctx, mode, cls):
    """Adding two caches using the same cache device
    Check that OCF does not allow for 2 caches using the same cache device to be started
    """

    cache_device = Volume(Size.from_MiB(20))
    cache = Cache.start_on_device(
        cache_device, cache_mode=mode, cache_line_size=cls, name="cache1"
    )
    cache.get_stats()

    with pytest.raises(OcfError, match="OCF_ERR_NOT_OPEN_EXC"):
        cache = Cache.start_on_device(
            cache_device, cache_mode=mode, cache_line_size=cls, name="cache2"
        )
    cache.get_stats()


@pytest.mark.parametrize("mode", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_start_too_small_device(pyocf_ctx, mode, cls):
    """Starting cache with device below 100MiB
    Check if starting cache with device below minimum size is blocked
    """

    cache_device = Volume(Size.from_B(20 * 1024 * 1024 - 1))

    with pytest.raises(OcfError, match="OCF_ERR_INVAL_CACHE_DEV"):
        Cache.start_on_device(cache_device, cache_mode=mode, cache_line_size=cls)


def test_start_stop_noqueue(pyocf_ctx):
    # cache object just to construct cfg conveniently
    _cache = Cache(pyocf_ctx.ctx_handle)

    cache_handle = c_void_p()
    status = pyocf_ctx.lib.ocf_mngt_cache_start(
        pyocf_ctx.ctx_handle, byref(cache_handle), byref(_cache.cfg), None
    )
    assert not status, "Failed to start cache: {}".format(status)

    # stop without creating mngmt queue
    c = OcfCompletion(
        [("cache", c_void_p), ("priv", c_void_p), ("error", c_int)]
    )
    pyocf_ctx.lib.ocf_mngt_cache_stop(cache_handle, c, None)
    c.wait()
    assert not c.results["error"], "Failed to stop cache: {}".format(c.results["error"])


def run_io_and_cache_data_if_possible(exported_obj, mode, cls, cls_no):
    test_data = Data(cls_no * cls)

    if mode in {CacheMode.WI, CacheMode.WA}:
        logger.info("[STAGE] Write to core device")
        io_to_core(exported_obj, test_data, 0, True)
        logger.info("[STAGE] Read from exported object")
        io_from_exported_object(exported_obj, test_data.size, 0)
    else:
        logger.info("[STAGE] Write to exported object")
        io_to_core(exported_obj, test_data, 0)

    stats = exported_obj.cache.get_stats()
    assert stats["usage"]["occupancy"]["value"] == \
        ((cls_no * cls / CacheLineSize.LINE_4KiB) if mode != CacheMode.PT else 0), "Occupancy"


def io_to_core(exported_obj: Core, data: Data, offset: int, to_core_device=False):
    new_io = exported_obj.new_core_io if to_core_device else exported_obj.new_io
    io = new_io(exported_obj.cache.get_default_queue(), offset, data.size,
                IoDir.WRITE, 0, 0)
    io.set_data(data)

    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    io.submit()
    completion.wait()

    assert completion.results["err"] == 0, "IO to exported object completion"


def io_from_exported_object(exported_obj: Core, buffer_size: int, offset: int):
    read_buffer = Data(buffer_size)
    io = exported_obj.new_io(exported_obj.cache.get_default_queue(), offset,
                             read_buffer.size, IoDir.READ, 0, 0)
    io.set_data(read_buffer)

    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    io.submit()
    completion.wait()

    assert completion.results["err"] == 0, "IO from exported object completion"
    return read_buffer


def check_stats_read_empty(exported_obj: Core, mode: CacheMode, cls: CacheLineSize):
    stats = exported_obj.cache.get_stats()
    assert stats["conf"]["cache_mode"] == mode, "Cache mode"
    assert exported_obj.cache.device.get_stats()[IoDir.WRITE] == (1 if mode.read_insert() else 0), \
        "Writes to cache device"
    assert exported_obj.device.get_stats()[IoDir.READ] == 1, "Reads from core device"
    assert stats["req"]["rd_full_misses"]["value"] == (0 if mode == CacheMode.PT else 1), \
        "Read full misses"
    assert stats["usage"]["occupancy"]["value"] == \
        ((cls / CacheLineSize.LINE_4KiB) if mode.read_insert() else 0), "Occupancy"


def check_stats_write_empty(exported_obj: Core, mode: CacheMode, cls: CacheLineSize):
    stats = exported_obj.cache.get_stats()
    assert stats["conf"]["cache_mode"] == mode, "Cache mode"
    # TODO(ajrutkow): why 1 for WT ??
    assert exported_obj.cache.device.get_stats()[IoDir.WRITE] == \
        (2 if mode.lazy_write() else (1 if mode == CacheMode.WT else 0)), \
        "Writes to cache device"
    assert exported_obj.device.get_stats()[IoDir.WRITE] == (0 if mode.lazy_write() else 1), \
        "Writes to core device"
    assert stats["req"]["wr_full_misses"]["value"] == (1 if mode.write_insert() else 0), \
        "Write full misses"
    assert stats["usage"]["occupancy"]["value"] == \
        ((cls / CacheLineSize.LINE_4KiB) if mode.write_insert() else 0), \
        "Occupancy"


def check_stats_write_after_read(exported_obj: Core,
                                 mode: CacheMode,
                                 cls: CacheLineSize,
                                 read_from_empty=False):
    stats = exported_obj.cache.get_stats()
    assert exported_obj.cache.device.get_stats()[IoDir.WRITE] == \
        (0 if mode in {CacheMode.WI, CacheMode.PT} else
            (2 if read_from_empty and mode.lazy_write() else 1)), \
        "Writes to cache device"
    assert exported_obj.device.get_stats()[IoDir.WRITE] == (0 if mode.lazy_write() else 1), \
        "Writes to core device"
    assert stats["req"]["wr_hits"]["value"] == \
        (1 if (mode.read_insert() and mode != CacheMode.WI)
            or (mode.write_insert() and not read_from_empty) else 0), \
        "Write hits"
    assert stats["usage"]["occupancy"]["value"] == \
        (0 if mode in {CacheMode.WI, CacheMode.PT} else (cls / CacheLineSize.LINE_4KiB)), \
        "Occupancy"


def check_stats_read_after_write(exported_obj, mode, cls, write_to_empty=False):
    stats = exported_obj.cache.get_stats()
    assert exported_obj.cache.device.get_stats()[IoDir.WRITE] == \
        (2 if mode.lazy_write() else (0 if mode == CacheMode.PT else 1)), \
        "Writes to cache device"
    assert exported_obj.cache.device.get_stats()[IoDir.READ] == \
        (1 if mode in {CacheMode.WT, CacheMode.WB, CacheMode.WO}
            or (mode == CacheMode.WA and not write_to_empty) else 0), \
        "Reads from cache device"
    assert exported_obj.device.get_stats()[IoDir.READ] == \
        (0 if mode in {CacheMode.WB, CacheMode.WO, CacheMode.WT}
            or (mode == CacheMode.WA and not write_to_empty) else 1), \
        "Reads from core device"
    assert stats["req"]["rd_full_misses"]["value"] == \
        (1 if mode in {CacheMode.WA, CacheMode.WI} else 0) \
        + (0 if write_to_empty or mode in {CacheMode.PT, CacheMode.WA} else 1), \
        "Read full misses"
    assert stats["req"]["rd_hits"]["value"] == \
        (1 if mode in {CacheMode.WT, CacheMode.WB, CacheMode.WO}
            or (mode == CacheMode.WA and not write_to_empty) else 0), \
        "Read hits"
    assert stats["usage"]["occupancy"]["value"] == \
        (0 if mode == CacheMode.PT else (cls / CacheLineSize.LINE_4KiB)), "Occupancy"


def check_md5_sums(exported_obj: Core, mode: CacheMode):
    if mode.lazy_write():
        assert exported_obj.device.md5() != exported_obj.exp_obj_md5(), \
            "MD5 check: core device vs exported object without flush"
        exported_obj.cache.flush()
        assert exported_obj.device.md5() == exported_obj.exp_obj_md5(), \
            "MD5 check: core device vs exported object after flush"
    else:
        assert exported_obj.device.md5() == exported_obj.exp_obj_md5(), \
            "MD5 check: core device vs exported object"


def get_cache_by_name(ctx, cache_name):
    cache_pointer = c_void_p()
    return OcfLib.getInstance().ocf_mngt_cache_get_by_name(
        ctx.ctx_handle, cache_name, byref(cache_pointer)
    )
