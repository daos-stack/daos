#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import c_int
import pytest
import math

from pyocf.types.cache import Cache, PromotionPolicy, NhitParams
from pyocf.types.core import Core
from pyocf.types.volume import Volume
from pyocf.types.data import Data
from pyocf.types.io import IoDir
from pyocf.utils import Size
from pyocf.types.shared import OcfCompletion


@pytest.mark.parametrize("promotion_policy", PromotionPolicy)
def test_init_nhit(pyocf_ctx, promotion_policy):
    """
    Check if starting cache with promotion policy is reflected in stats

    1. Create core/cache pair with parametrized promotion policy
    2. Get cache statistics
        * verify that promotion policy type is properly reflected in stats
    """

    cache_device = Volume(Size.from_MiB(30))
    core_device = Volume(Size.from_MiB(30))

    cache = Cache.start_on_device(cache_device, promotion_policy=promotion_policy)
    core = Core.using_device(core_device)

    cache.add_core(core)

    assert cache.get_stats()["conf"]["promotion_policy"] == promotion_policy


def test_change_to_nhit_and_back_io_in_flight(pyocf_ctx):
    """
    Try switching promotion policy during io, no io's should return with error

    1. Create core/cache pair with promotion policy ALWAYS
    2. Issue IOs without waiting for completion
    3. Change promotion policy to NHIT
    4. Wait for IO completions
        * no IOs should fail
    5. Issue IOs without waiting for completion
    6. Change promotion policy to ALWAYS
    7. Wait for IO completions
        * no IOs should fail
    """

    # Step 1
    cache_device = Volume(Size.from_MiB(30))
    core_device = Volume(Size.from_MiB(30))

    cache = Cache.start_on_device(cache_device)
    core = Core.using_device(core_device)

    cache.add_core(core)

    # Step 2
    completions = []
    for i in range(2000):
        comp = OcfCompletion([("error", c_int)])
        write_data = Data(4096)
        io = core.new_io(
            cache.get_default_queue(), i * 4096, write_data.size, IoDir.WRITE, 0, 0
        )
        completions += [comp]
        io.set_data(write_data)
        io.callback = comp.callback
        io.submit()

    # Step 3
    cache.set_promotion_policy(PromotionPolicy.NHIT)

    # Step 4
    for c in completions:
        c.wait()
        assert not c.results["error"], "No IO's should fail when turning NHIT policy on"

    # Step 5
    completions = []
    for i in range(2000):
        comp = OcfCompletion([("error", c_int)])
        write_data = Data(4096)
        io = core.new_io(
            cache.get_default_queue(), i * 4096, write_data.size, IoDir.WRITE, 0, 0
        )
        completions += [comp]
        io.set_data(write_data)
        io.callback = comp.callback
        io.submit()

    # Step 6
    cache.set_promotion_policy(PromotionPolicy.ALWAYS)

    # Step 7
    for c in completions:
        c.wait()
        assert not c.results[
            "error"
        ], "No IO's should fail when turning NHIT policy off"


def fill_cache(cache, fill_ratio):
    """
    Helper to fill cache from LBA 0.
    TODO:
        * make it generic and share across all tests
        * reasonable error handling
    """

    cache_lines = cache.get_stats()["conf"]["size"]

    bytes_to_fill = cache_lines.bytes * fill_ratio
    max_io_size = cache.device.get_max_io_size().bytes

    ios_to_issue = math.floor(bytes_to_fill / max_io_size)

    core = cache.cores[0]
    completions = []
    for i in range(ios_to_issue):
        comp = OcfCompletion([("error", c_int)])
        write_data = Data(max_io_size)
        io = core.new_io(
            cache.get_default_queue(),
            i * max_io_size,
            write_data.size,
            IoDir.WRITE,
            0,
            0,
        )
        io.set_data(write_data)
        io.callback = comp.callback
        completions += [comp]
        io.submit()

    if bytes_to_fill % max_io_size:
        comp = OcfCompletion([("error", c_int)])
        write_data = Data(Size.from_B(bytes_to_fill % max_io_size, sector_aligned=True))
        io = core.new_io(
            cache.get_default_queue(),
            ios_to_issue * max_io_size,
            write_data.size,
            IoDir.WRITE,
            0,
            0,
        )
        io.set_data(write_data)
        io.callback = comp.callback
        completions += [comp]
        io.submit()

    for c in completions:
        c.wait()


@pytest.mark.parametrize("fill_percentage", [0, 1, 50, 99])
@pytest.mark.parametrize("insertion_threshold", [2, 8])
def test_promoted_after_hits_various_thresholds(
    pyocf_ctx, insertion_threshold, fill_percentage
):
    """
    Check promotion policy behavior with various set thresholds

    1. Create core/cache pair with promotion policy NHIT
    2. Set TRIGGER_THRESHOLD/INSERTION_THRESHOLD to predefined values
    3. Fill cache from the beggining until occupancy reaches TRIGGER_THRESHOLD%
    4. Issue INSERTION_THRESHOLD - 1 requests to core line not inserted to cache
        * occupancy should not change
    5. Issue one request to LBA from step 4
        * occupancy should rise by one cache line
    """

    # Step 1
    cache_device = Volume(Size.from_MiB(30))
    core_device = Volume(Size.from_MiB(30))

    cache = Cache.start_on_device(cache_device, promotion_policy=PromotionPolicy.NHIT)
    core = Core.using_device(core_device)
    cache.add_core(core)

    # Step 2
    cache.set_promotion_policy_param(
        PromotionPolicy.NHIT, NhitParams.TRIGGER_THRESHOLD, fill_percentage
    )
    cache.set_promotion_policy_param(
        PromotionPolicy.NHIT, NhitParams.INSERTION_THRESHOLD, insertion_threshold
    )
    # Step 3
    fill_cache(cache, fill_percentage / 100)

    stats = cache.get_stats()
    cache_lines = stats["conf"]["size"]
    assert stats["usage"]["occupancy"]["fraction"] // 10 == fill_percentage * 10
    filled_occupancy = stats["usage"]["occupancy"]["value"]

    # Step 4
    last_core_line = int(core_device.size) - cache_lines.line_size
    completions = []
    for i in range(insertion_threshold - 1):
        comp = OcfCompletion([("error", c_int)])
        write_data = Data(cache_lines.line_size)
        io = core.new_io(
            cache.get_default_queue(),
            last_core_line,
            write_data.size,
            IoDir.WRITE,
            0,
            0,
        )
        completions += [comp]
        io.set_data(write_data)
        io.callback = comp.callback
        io.submit()

    for c in completions:
        c.wait()

    stats = cache.get_stats()
    threshold_reached_occupancy = stats["usage"]["occupancy"]["value"]
    assert threshold_reached_occupancy == filled_occupancy, (
        "No insertion should occur while NHIT is triggered and core line ",
        "didn't reach INSERTION_THRESHOLD",
    )

    # Step 5
    comp = OcfCompletion([("error", c_int)])
    write_data = Data(cache_lines.line_size)
    io = core.new_io(
        cache.get_default_queue(), last_core_line, write_data.size, IoDir.WRITE, 0, 0
    )
    io.set_data(write_data)
    io.callback = comp.callback
    io.submit()

    comp.wait()

    assert (
        threshold_reached_occupancy
        == cache.get_stats()["usage"]["occupancy"]["value"] - 1
    ), "Previous request should be promoted and occupancy should rise"


def test_partial_hit_promotion(pyocf_ctx):
    """
    Check if NHIT promotion policy doesn't prevent partial hits from getting
    promoted to cache

    1. Create core/cache pair with promotion policy ALWAYS
    2. Issue one-sector IO to cache to insert partially valid cache line
    3. Set NHIT promotion policy with trigger=0 (always triggered) and high
    insertion threshold
    4. Issue a request containing partially valid cache line and next cache line
        * occupancy should rise - partially hit request should bypass nhit criteria
    """

    # Step 1
    cache_device = Volume(Size.from_MiB(30))
    core_device = Volume(Size.from_MiB(30))

    cache = Cache.start_on_device(cache_device)
    core = Core.using_device(core_device)
    cache.add_core(core)

    # Step 2
    comp = OcfCompletion([("error", c_int)])
    write_data = Data(Size.from_sector(1))
    io = core.new_io(cache.get_default_queue(), 0, write_data.size, IoDir.READ, 0, 0)
    io.set_data(write_data)
    io.callback = comp.callback
    io.submit()

    comp.wait()

    stats = cache.get_stats()
    cache_lines = stats["conf"]["size"]
    assert stats["usage"]["occupancy"]["value"] == 1

    # Step 3
    cache.set_promotion_policy(PromotionPolicy.NHIT)
    cache.set_promotion_policy_param(
        PromotionPolicy.NHIT, NhitParams.TRIGGER_THRESHOLD, 0
    )
    cache.set_promotion_policy_param(
        PromotionPolicy.NHIT, NhitParams.INSERTION_THRESHOLD, 100
    )

    # Step 4
    comp = OcfCompletion([("error", c_int)])
    write_data = Data(2 * cache_lines.line_size)
    io = core.new_io(cache.get_default_queue(), 0, write_data.size, IoDir.WRITE, 0, 0)
    io.set_data(write_data)
    io.callback = comp.callback
    io.submit()
    comp.wait()

    stats = cache.get_stats()
    assert (
        stats["usage"]["occupancy"]["value"] == 2
    ), "Second cache line should be mapped"
