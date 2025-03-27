#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import pytest
from ctypes import c_int

from pyocf.types.cache import Cache, CacheMode
from pyocf.types.core import Core
from pyocf.types.volume import Volume
from pyocf.utils import Size as S
from pyocf.types.data import Data, DataOps
from pyocf.types.ctx import OcfCtx
from pyocf.types.logger import DefaultLogger, LogLevel
from pyocf.ocf import OcfLib
from pyocf.types.cleaner import Cleaner
from pyocf.types.io import IoDir
from pyocf.types.shared import OcfCompletion


class DataCopyTracer(Data):
    """
        This class enables tracking whether each copied over Data instance is
        then securely erased.
    """

    needs_erase = set()
    locked_instances = set()

    @staticmethod
    @DataOps.ALLOC
    def _alloc(pages):
        data = DataCopyTracer.pages(pages)
        Data._ocf_instances_.append(data)

        return data.handle.value

    def mlock(self):
        DataCopyTracer.locked_instances.add(self)
        DataCopyTracer.needs_erase.add(self)
        return super().mlock()

    def munlock(self):
        if self in DataCopyTracer.needs_erase:
            assert 0, "Erase should be called first on locked Data!"

        DataCopyTracer.locked_instances.remove(self)
        return super().munlock()

    def secure_erase(self):
        DataCopyTracer.needs_erase.remove(self)
        return super().secure_erase()

    def copy(self, src, end, start, size):
        DataCopyTracer.needs_erase.add(self)
        return super().copy(src, end, start, size)


@pytest.mark.security
@pytest.mark.parametrize(
    "cache_mode", [CacheMode.WT, CacheMode.WB, CacheMode.WA, CacheMode.WI]
)
def test_secure_erase_simple_io_read_misses(cache_mode):
    """
        Perform simple IO which will trigger read misses, which in turn should
        trigger backfill. Track all the data locked/copied for backfill and make
        sure OCF calls secure erase and unlock on them.
    """
    ctx = OcfCtx(
        OcfLib.getInstance(),
        b"Security tests ctx",
        DefaultLogger(LogLevel.WARN),
        DataCopyTracer,
        Cleaner,
    )

    ctx.register_volume_type(Volume)

    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cache_mode)

    core_device = Volume(S.from_MiB(50))
    core = Core.using_device(core_device)
    cache.add_core(core)

    write_data = DataCopyTracer(S.from_sector(1))
    io = core.new_io(
        cache.get_default_queue(),
        S.from_sector(1).B,
        write_data.size,
        IoDir.WRITE,
        0,
        0,
    )
    io.set_data(write_data)

    cmpl = OcfCompletion([("err", c_int)])
    io.callback = cmpl.callback
    io.submit()
    cmpl.wait()

    cmpls = []
    for i in range(100):
        read_data = DataCopyTracer(S.from_sector(1))
        io = core.new_io(
            cache.get_default_queue(),
            i * S.from_sector(1).B,
            read_data.size,
            IoDir.READ,
            0,
            0,
        )
        io.set_data(read_data)

        cmpl = OcfCompletion([("err", c_int)])
        io.callback = cmpl.callback
        cmpls.append(cmpl)
        io.submit()

    for c in cmpls:
        c.wait()

    write_data = DataCopyTracer.from_string("TEST DATA" * 100)
    io = core.new_io(
        cache.get_default_queue(), S.from_sector(1), write_data.size, IoDir.WRITE, 0, 0
    )
    io.set_data(write_data)

    cmpl = OcfCompletion([("err", c_int)])
    io.callback = cmpl.callback
    io.submit()
    cmpl.wait()

    stats = cache.get_stats()

    ctx.exit()

    assert (
        len(DataCopyTracer.needs_erase) == 0
    ), "Not all locked Data instances were secure erased!"
    assert (
        len(DataCopyTracer.locked_instances) == 0
    ), "Not all locked Data instances were unlocked!"
    assert (
        stats["req"]["rd_partial_misses"]["value"]
        + stats["req"]["rd_full_misses"]["value"]
    ) > 0


@pytest.mark.security
def test_secure_erase_simple_io_cleaning():
    """
        Perform simple IO which will trigger WB cleaning. Track all the data from
        cleaner (locked) and make sure they are erased and unlocked after use.

        1. Start cache in WB mode
        2. Write single sector at LBA 0
        3. Read whole cache line at LBA 0
        4. Assert that 3. triggered cleaning
        5. Check if all locked Data copies were erased and unlocked
    """
    ctx = OcfCtx(
        OcfLib.getInstance(),
        b"Security tests ctx",
        DefaultLogger(LogLevel.WARN),
        DataCopyTracer,
        Cleaner,
    )

    ctx.register_volume_type(Volume)

    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=CacheMode.WB)

    core_device = Volume(S.from_MiB(100))
    core = Core.using_device(core_device)
    cache.add_core(core)

    read_data = Data(S.from_sector(1).B)
    io = core.new_io(
        cache.get_default_queue(), S.from_sector(1).B, read_data.size, IoDir.WRITE, 0, 0
    )
    io.set_data(read_data)

    cmpl = OcfCompletion([("err", c_int)])
    io.callback = cmpl.callback
    io.submit()
    cmpl.wait()

    read_data = Data(S.from_sector(8).B)
    io = core.new_io(
        cache.get_default_queue(), S.from_sector(1).B, read_data.size, IoDir.READ, 0, 0
    )
    io.set_data(read_data)

    cmpl = OcfCompletion([("err", c_int)])
    io.callback = cmpl.callback
    io.submit()
    cmpl.wait()

    stats = cache.get_stats()

    ctx.exit()

    assert (
        len(DataCopyTracer.needs_erase) == 0
    ), "Not all locked Data instances were secure erased!"
    assert (
        len(DataCopyTracer.locked_instances) == 0
    ), "Not all locked Data instances were unlocked!"
    assert (stats["usage"]["clean"]["value"]) > 0, "Cleaner didn't run!"
