# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import c_int, memmove, cast, c_void_p
from enum import IntEnum
from itertools import product
from itertools import repeat
import pytest
import random
from hashlib import md5
from datetime import datetime

from pyocf.types.cache import Cache, CacheMode
from pyocf.types.core import Core
from pyocf.types.volume import Volume
from pyocf.types.data import Data
from pyocf.types.io import IoDir
from pyocf.utils import Size
from pyocf.types.shared import OcfCompletion, CacheLineSize


def get_byte(number, byte):
    return (number & (0xFF << (byte * 8))) >> (byte * 8)


def bytes_to_uint32(byte0, byte1, byte2, byte3):
    return (int(byte3) << 24) + (int(byte2) << 16) + (int(byte1) << 8) + int(byte0)


def __io(io, queue, address, size, data, direction):
    io.set_data(data, 0)
    completion = OcfCompletion([("err", c_int)])
    io.callback = completion.callback
    io.submit()
    completion.wait()
    return int(completion.results["err"])


def _io(new_io, queue, address, size, data, offset, direction):
    io = new_io(queue, address, size, direction, 0, 0)
    if direction == IoDir.READ:
        _data = Data.from_bytes(bytes(size))
    else:
        _data = Data.from_bytes(data, offset, size)
    ret = __io(io, queue, address, size, _data, direction)
    if not ret and direction == IoDir.READ:
        memmove(cast(data, c_void_p).value + offset, _data.handle, size)
    return ret


def io_to_core(core, address, size, data, offset, direction):
    return _io(
        core.new_core_io,
        core.cache.get_default_queue(),
        address,
        size,
        data,
        offset,
        direction,
    )


def io_to_exp_obj(core, address, size, data, offset, direction):
    return _io(
        core.new_io,
        core.cache.get_default_queue(),
        address,
        size,
        data,
        offset,
        direction,
    )


def sector_to_region(sector, region_start):
    num_regions = len(region_start)
    i = 0
    while i < num_regions - 1 and sector >= region_start[i + 1]:
        i += 1
    return i


def region_end(region_start, region_no, total_sectors):
    num_regions = len(region_start)
    return (
        region_start[region_no + 1] - 1
        if region_no < num_regions - 1
        else total_sectors - 1
    )


class SectorStatus(IntEnum):
    INVALID = (0,)
    CLEAN = (1,)
    DIRTY = (2,)


def sector_status_to_char(status):
    if status == SectorStatus.INVALID:
        return "I"
    if status == SectorStatus.DIRTY:
        return "D"
    if status == SectorStatus.CLEAN:
        return "C"


I = SectorStatus.INVALID
D = SectorStatus.DIRTY
C = SectorStatus.CLEAN


# Print test case description for debug/informational purposes. Example output (for
# 4k cacheline):
#    |8C|8C>|8C|7CD|3IC<2C2I|C7I|8I|8I|8I|
#
# - pipe character represents cacheline boundary
# - letters represent sector status ((D)irty, (C)lean, (I)nvalid)
# - numbers represent number of consecutive sectors with the same staus (e.g. '3I' means
#    3 invalid sectors). No number (e.g. 'D') means one sector.
# - '>' and '<' characters represent I/O target adress range
def print_test_case(
    reg_start_sec, region_state, io_start, io_end, total_sectors, sectors_per_cacheline
):
    cl_strted = -1

    sec = 0
    while sec <= total_sectors:
        if io_start == sec:
            print(">", end="")

        if sec % sectors_per_cacheline == 0:
            print("|", end="")

        if io_end == sec - 1:
            print("<", end="")

        if sec == total_sectors:
            break

        cl_boundary_dist = sectors_per_cacheline - (sec % sectors_per_cacheline)
        io_start_dist = io_start - sec if io_start > sec else 2 * total_sectors
        io_end_dist = io_end - sec + 1 if io_end >= sec else 2 * total_sectors
        next_sec_dist = min(cl_boundary_dist, io_start_dist, io_end_dist)

        # move up as much as @next_sec_dist sectors as long as they're in the same state
        reg = sector_to_region(sec, reg_start_sec)
        state = region_state[reg]
        i = 0
        regch_end_dist = 0
        while (
            reg + i < len(reg_start_sec)
            and state == region_state[reg + i]
            and regch_end_dist < next_sec_dist
        ):
            regch_end_dist = region_end(reg_start_sec, reg + i, total_sectors) - sec + 1
            i += 1

        next_sec_dist = min(next_sec_dist, regch_end_dist)

        if next_sec_dist > 1:
            print("{}{}".format(next_sec_dist, sector_status_to_char(state)), end="")
        else:
            print("{}".format(sector_status_to_char(state)), end="")

        sec += next_sec_dist
    assert sec == total_sectors or sec == reg_start_sec[region + 1]

    print("")


# Test reads with with different combinations of sectors status and IO range.
# Nine consecutive core lines are targeted, with the middle one (no 4)
# having all sectors status (clean, dirty, invalid) set independently. Neighbouring
# two lines either are fully dirty/clean/invalid or have a different status for a single
# sector neighbouring with middle core line  The first and the last three cachelines
# both constitute a single region and each triple is always fully dirty/clean/invalid.
# This gives total of at least 14 regions with independent state (4k cacheline case). The below
# diagram depicts 4k cacheline case:
#
# cache line         |  CL 0  |  CL 1  |  CL 2  | CL 3   |  CL 4  | CL 5   |  CL 6  |  CL 7  |  CL 8  |
# 512 sector no      |01234567|89ABCDEF|(ctd..) |  ...   |  ...   |  ...   |  ...   |  ...   |  ...   |
# test region no     |00000000|00000000|00000000|11111112|3456789A|BCCCCCCC|DDDDDDDD|DDDDDDDD|DDDDDDDD|
# test region start? |*-------|--------|--------|*------*|********|**------|*-------|--------|--------|
# io start possible  |        |        |        |        |        |        |        |        |        |
#   values @START    |>       |>       |>       |>     >>|>>>>>>>>|        |        |        |        |
# io end possible    |        |        |        |        |        |        |        |        |        |
#   values @END      |        |        |       <|        |<<<<<<<<|<<     <|       <|       <|       <|
#
# Each test iteration is described by region states and IO start/end sectors,
# giving total of (cacheline_size / 512B) + 8 parameters:
#   - 1 region state for cachelines 0-2
#   - 2 region states for cacheline 3
#   - (cacheline_size / 512B) region states for cacheline 4 (1 for each sector in cacheline)
#   - 2 region states for and cacheline 5
#   - 1 region state for cachelines 6-8
#   - IO start and end sector
#
# In order to determine data consistency, drives are filled with 32-bit pattern:
# - core sector no @n *not* promoted to cache (invalid sector) is filled with (@n << 2)  + 0
# - cache and core clean sector no @n is filled with (@n << 2) + 1
# - cache sector no @n containing dirty data is filled with (@n << 2) + 2
#
# This data pattern is enforced by writing to exported object in the following order:
#  1. writing entire workset with core patern in PT
#  2. writing clean sectors with clean pattern in WT
#  3. writing dirty sectors with dirty pattern in WB
#
# Then the verification is simply a matter of issuing a read in selected cache mode
# and verifying  that the expected pattern is read from each sector.
#


@pytest.mark.parametrize("cacheline_size", CacheLineSize)
@pytest.mark.parametrize("cache_mode", CacheMode)
@pytest.mark.parametrize("rand_seed", [datetime.now()])
def test_read_data_consistency(pyocf_ctx, cacheline_size, cache_mode, rand_seed):
    CACHELINE_COUNT = 9
    SECTOR_SIZE = Size.from_sector(1).B
    CLS = cacheline_size // SECTOR_SIZE
    WORKSET_SIZE = CACHELINE_COUNT * cacheline_size
    WORKSET_OFFSET = 128 * cacheline_size
    SECTOR_COUNT = int(WORKSET_SIZE / SECTOR_SIZE)
    ITRATION_COUNT = 50

    random.seed(rand_seed)

    # start sector for each region (positions of '*' on the above diagram)
    region_start = (
        [0, 3 * CLS, 4 * CLS - 1]
        + [4 * CLS + i for i in range(CLS)]
        + [5 * CLS, 5 * CLS + 1, 6 * CLS]
    )
    num_regions = len(region_start)
    # possible IO start sectors for test iteration  (positions of '>' on the above diagram)
    start_sec = [0, CLS, 2 * CLS, 3 * CLS, 4 * CLS - 2, 4 * CLS - 1] + [
        4 * CLS + i for i in range(CLS)
    ]
    # possible IO end sectors for test iteration (positions o '<' on the above diagram)
    end_sec = (
        [3 * CLS - 1]
        + [4 * CLS + i for i in range(CLS)]
        + [5 * CLS, 5 * CLS + 1, 6 * CLS - 1, 7 * CLS - 1, 8 * CLS - 1, 9 * CLS - 1]
    )

    data = {}
    # memset n-th sector of core data with n << 2
    data[SectorStatus.INVALID] = bytes(
        [get_byte(((x // SECTOR_SIZE) << 2) + 0, x % 4) for x in range(WORKSET_SIZE)]
    )
    # memset n-th sector of clean data with n << 2 + 1
    data[SectorStatus.CLEAN] = bytes(
        [get_byte(((x // SECTOR_SIZE) << 2) + 1, x % 4) for x in range(WORKSET_SIZE)]
    )
    # memset n-th sector of dirty data with n << 2 + 2
    data[SectorStatus.DIRTY] = bytes(
        [get_byte(((x // SECTOR_SIZE) << 2) + 2, x % 4) for x in range(WORKSET_SIZE)]
    )

    result_b = bytes(WORKSET_SIZE)

    cache_device = Volume(Size.from_MiB(30))
    core_device = Volume(Size.from_MiB(30))

    cache = Cache.start_on_device(
        cache_device, cache_mode=CacheMode.WO, cache_line_size=cacheline_size
    )
    core = Core.using_device(core_device)

    cache.add_core(core)

    insert_order = list(range(CACHELINE_COUNT))

    # set fixed generated sector statuses
    region_statuses = [
        [I, I, I] + [I for i in range(CLS)] + [I, I, I],
        [I, I, I] + [D for i in range(CLS)] + [I, I, I],
        [I, I, I] + [C for i in range(CLS)] + [I, I, I],
        [I, I, I]
        + [D for i in range(CLS // 2 - 1)]
        + [I]
        + [D for i in range(CLS // 2)]
        + [I, I, I],
        [I, I, I]
        + [D for i in range(CLS // 2 - 1)]
        + [I, I]
        + [D for i in range(CLS // 2 - 1)]
        + [I, I, I],
        [I, I, I]
        + [D for i in range(CLS // 2 - 2)]
        + [I, I, D, C]
        + [D for i in range(CLS // 2 - 2)]
        + [I, I, I],
        [I, I, D] + [D for i in range(CLS)] + [D, I, I],
        [I, I, D]
        + [D for i in range(CLS // 2 - 1)]
        + [I]
        + [D for i in range(CLS // 2)]
        + [D, I, I],
    ]

    # add randomly generated sector statuses
    for _ in range(ITRATION_COUNT - len(region_statuses)):
        region_statuses.append(
            [random.choice(list(SectorStatus)) for _ in range(num_regions)]
        )

    # iterate over generated status combinations and perform the test
    for region_state in region_statuses:
        # write data to core and invalidate all CL and write data pattern to core
        cache.change_cache_mode(cache_mode=CacheMode.PT)
        io_to_exp_obj(
            core,
            WORKSET_OFFSET,
            len(data[SectorStatus.INVALID]),
            data[SectorStatus.INVALID],
            0,
            IoDir.WRITE,
        )

        # randomize cacheline insertion order to exercise different
        # paths with regard to cache I/O physical addresses continuousness
        random.shuffle(insert_order)
        sectors = [
            insert_order[i // CLS] * CLS + (i % CLS) for i in range(SECTOR_COUNT)
        ]

        # insert clean sectors - iterate over cachelines in @insert_order order
        cache.change_cache_mode(cache_mode=CacheMode.WT)
        for sec in sectors:
            region = sector_to_region(sec, region_start)
            if region_state[region] != SectorStatus.INVALID:
                io_to_exp_obj(
                    core,
                    WORKSET_OFFSET + SECTOR_SIZE * sec,
                    SECTOR_SIZE,
                    data[SectorStatus.CLEAN],
                    sec * SECTOR_SIZE,
                    IoDir.WRITE,
                )

        # write dirty sectors
        cache.change_cache_mode(cache_mode=CacheMode.WB)
        for sec in sectors:
            region = sector_to_region(sec, region_start)
            if region_state[region] == SectorStatus.DIRTY:
                io_to_exp_obj(
                    core,
                    WORKSET_OFFSET + SECTOR_SIZE * sec,
                    SECTOR_SIZE,
                    data[SectorStatus.DIRTY],
                    sec * SECTOR_SIZE,
                    IoDir.WRITE,
                )

        cache.change_cache_mode(cache_mode=cache_mode)

        core_device.reset_stats()

        # get up to 32 randomly selected pairs of (start,end) sectors
        # 32 is enough to cover all combinations for 4K and 8K cacheline size
        io_ranges = [(s, e) for s, e in product(start_sec, end_sec) if s < e]
        random.shuffle(io_ranges)
        io_ranges = io_ranges[:32]

        # run the test for each selected IO range for currently set up region status
        for start, end in io_ranges:
            print_test_case(region_start, region_state, start, end, SECTOR_COUNT, CLS)

            # issue read
            START = start * SECTOR_SIZE
            END = end * SECTOR_SIZE
            size = (end - start + 1) * SECTOR_SIZE
            assert 0 == io_to_exp_obj(
                core, WORKSET_OFFSET + START, size, result_b, START, IoDir.READ
            ), "error reading in {}: region_state={}, start={}, end={}, insert_order={}".format(
                cache_mode, region_state, start, end, insert_order
            )

            # verify read data
            for sec in range(start, end + 1):
                # just check the first 32bits of sector (this is the size of fill pattern)
                region = sector_to_region(sec, region_start)
                start_byte = sec * SECTOR_SIZE
                expected_data = bytes_to_uint32(
                    data[region_state[region]][start_byte + 0],
                    data[region_state[region]][start_byte + 1],
                    data[region_state[region]][start_byte + 2],
                    data[region_state[region]][start_byte + 3],
                )
                actual_data = bytes_to_uint32(
                    result_b[start_byte + 0],
                    result_b[start_byte + 1],
                    result_b[start_byte + 2],
                    result_b[start_byte + 3],
                )

                assert (
                    actual_data == expected_data
                ), "unexpected data in sector {}, region_state={}, start={}, end={}, insert_order={}\n".format(
                    sec, region_state, start, end, insert_order
                )

            if cache_mode == CacheMode.WO:
                # WO is not supposed to clean dirty data
                assert (
                    core_device.get_stats()[IoDir.WRITE] == 0
                ), "unexpected write to core device, region_state={}, start={}, end={}, insert_order = {}\n".format(
                    region_state, start, end, insert_order
                )
