#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import logging
from math import ceil, isclose
from ctypes import c_int

import pytest

from pyocf.types.cache import Cache, CacheMode
from pyocf.types.core import Core
from pyocf.types.data import Data
from pyocf.types.io import IoDir
from pyocf.types.shared import OcfCompletion, CacheLineSize, SeqCutOffPolicy, CacheLines
from pyocf.types.volume import Volume
from pyocf.utils import Size

logger = logging.getLogger(__name__)


@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.parametrize("mode", [CacheMode.WT])
def test_eviction_two_cores(pyocf_ctx, mode: CacheMode, cls: CacheLineSize):
    """Test if eviction works correctly when remapping cachelines between distinct cores."""
    cache_device = Volume(Size.from_MiB(20))

    core_device1 = Volume(Size.from_MiB(40))
    core_device2 = Volume(Size.from_MiB(40))
    cache = Cache.start_on_device(cache_device, cache_mode=mode, cache_line_size=cls)
    cache.set_seq_cut_off_policy(SeqCutOffPolicy.NEVER)
    cache_size = cache.get_stats()["conf"]["size"]
    core_exported1 = Core.using_device(core_device1, name="core1")
    core_exported2 = Core.using_device(core_device2, name="core2")
    cache.add_core(core_exported1)
    cache.add_core(core_exported2)

    valid_io_size = Size.from_B(cache_size.B)
    test_data = Data(valid_io_size)
    send_io(core_exported1, test_data)
    send_io(core_exported2, test_data)

    stats1 = core_exported1.get_stats()
    stats2 = core_exported2.get_stats()
    # IO to the second core should evict all the data from the first core
    assert stats1["usage"]["occupancy"]["value"] == 0
    assert stats2["usage"]["occupancy"]["value"] == valid_io_size.blocks_4k


@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.parametrize("mode", [CacheMode.WT, CacheMode.WB, CacheMode.WO])
def test_write_size_greater_than_cache(pyocf_ctx, mode: CacheMode, cls: CacheLineSize):
    """Test if eviction does not occur when IO greater than cache size is submitted."""
    cache_device = Volume(Size.from_MiB(20))

    core_device = Volume(Size.from_MiB(5))
    cache = Cache.start_on_device(cache_device, cache_mode=mode, cache_line_size=cls)
    cache_size = cache.get_stats()["conf"]["size"]
    core_exported = Core.using_device(core_device)
    cache.add_core(core_exported)
    cache.set_seq_cut_off_policy(SeqCutOffPolicy.NEVER)

    valid_io_size = Size.from_B(cache_size.B // 2)
    test_data = Data(valid_io_size)
    send_io(core_exported, test_data)

    stats = core_exported.cache.get_stats()
    first_block_sts = stats["block"]
    first_usage_sts = stats["usage"]
    pt_writes_first = stats["req"]["wr_pt"]
    assert stats["usage"]["occupancy"]["value"] == (
        valid_io_size.B / Size.from_KiB(4).B
    ), "Occupancy after first IO"
    prev_writes_to_core = stats["block"]["core_volume_wr"]["value"]

    # Anything below 5 MiB is a valid size (less than core device size)
    # Writing over cache size (to the offset above first io) in this case should go
    # directly to core and shouldn't trigger eviction
    io_size_bigger_than_cache = Size.from_MiB(2)
    io_offset = valid_io_size
    test_data = Data(io_size_bigger_than_cache)
    send_io(core_exported, test_data, io_offset)

    if mode is not CacheMode.WT:
        # Flush first write
        cache.flush()
    stats = core_exported.cache.get_stats()
    second_block_sts = stats["block"]
    second_usage_sts = stats["usage"]
    pt_writes_second = stats["req"]["wr_pt"]

    # Second write shouldn't affect cache and should go directly to core.
    # Cache occupancy shouldn't change
    # Second IO should go in PT
    assert first_usage_sts["occupancy"] == second_usage_sts["occupancy"]
    assert pt_writes_first["value"] == 0
    assert pt_writes_second["value"] == 1
    assert second_block_sts["cache_volume_wr"]["value"] == valid_io_size.blocks_4k
    assert (
        second_block_sts["core_volume_wr"]["value"]
        == valid_io_size.blocks_4k + io_size_bigger_than_cache.blocks_4k
    )


@pytest.mark.parametrize("cls", CacheLineSize)
def test_evict_overflown_pinned(pyocf_ctx, cls: CacheLineSize):
    """ Verify if overflown pinned ioclass is evicted """
    cache_device = Volume(Size.from_MiB(35))
    core_device = Volume(Size.from_MiB(100))
    cache = Cache.start_on_device(
        cache_device, cache_mode=CacheMode.WT, cache_line_size=cls
    )
    core = Core.using_device(core_device)
    cache.add_core(core)

    test_ioclass_id = 1
    pinned_ioclass_id = 2
    pinned_ioclass_max_occupancy = 10

    cache.configure_partition(
        part_id=test_ioclass_id,
        name="default_ioclass",
        max_size=100,
        priority=1,
    )
    cache.configure_partition(
        part_id=pinned_ioclass_id,
        name="pinned_ioclass",
        max_size=pinned_ioclass_max_occupancy,
        priority=-1,
    )

    cache.set_seq_cut_off_policy(SeqCutOffPolicy.NEVER)

    cache_size = cache.get_stats()["conf"]["size"]

    data = Data(4096)

    # Populate cache with data
    for i in range(cache_size.blocks_4k):
        send_io(core, data, i * 4096, test_ioclass_id)

    part_current_size = CacheLines(
        cache.get_partition_info(part_id=test_ioclass_id)["_curr_size"], cls
    )
    assert isclose(
        part_current_size.blocks_4k, cache_size.blocks_4k, abs_tol=Size(cls).blocks_4k
    ), "Failed to populate the default partition"

    # Repart - force overflow of second partition occupancy limit
    pinned_double_size = ceil(
        (cache_size.blocks_4k * pinned_ioclass_max_occupancy * 2) / 100
    )
    for i in range(pinned_double_size):
        send_io(core, data, i * 4096, pinned_ioclass_id)

    part_current_size = CacheLines(
        cache.get_partition_info(part_id=pinned_ioclass_id)["_curr_size"], cls
    )
    assert isclose(
        part_current_size.blocks_4k, pinned_double_size, abs_tol=Size(cls).blocks_4k
    ), "Occupancy of pinned ioclass doesn't match expected value"

    # Trigger IO to the default ioclass - force eviction from overlown ioclass
    for i in range(cache_size.blocks_4k):
        send_io(core, data, (cache_size.blocks_4k + i) * 4096, test_ioclass_id)

    part_current_size = CacheLines(
        cache.get_partition_info(part_id=pinned_ioclass_id)["_curr_size"], cls
    )
    assert isclose(
        part_current_size.blocks_4k,
        ceil(cache_size.blocks_4k * 0.1),
        abs_tol=Size(cls).blocks_4k,
    ), "Overflown part has not been evicted"


def send_io(exported_obj: Core, data: Data, addr: int = 0, target_ioclass: int = 0):
    io = exported_obj.new_io(
        exported_obj.cache.get_default_queue(),
        addr,
        data.size,
        IoDir.WRITE,
        target_ioclass,
        0,
    )

    io.set_data(data)

    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    io.submit()
    completion.wait()

    assert completion.results["err"] == 0, "IO to exported object completion"
