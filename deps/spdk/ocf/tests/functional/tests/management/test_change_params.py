#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import pytest

from pyocf.types.cache import Cache, CacheMode, CleaningPolicy, SeqCutOffPolicy
from pyocf.types.core import Core
from pyocf.types.volume import Volume
from pyocf.utils import Size as S
from pyocf.types.shared import CacheLineSize


@pytest.mark.parametrize("from_cm", CacheMode)
@pytest.mark.parametrize("to_cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_change_cache_mode(pyocf_ctx, from_cm, to_cm, cls):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=from_cm, cache_line_size=cls
    )

    # Change cache mode and check if stats are as expected
    cache.change_cache_mode(to_cm)
    stats_after = cache.get_stats()
    assert stats_after["conf"]["cache_mode"] == to_cm


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_change_cleaning_policy(pyocf_ctx, cm, cls):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=cm, cache_line_size=cls
    )

    # Check all possible cleaning policy switches
    for cp_from in CleaningPolicy:
        for cp_to in CleaningPolicy:
            cache.set_cleaning_policy(cp_from.value)

            # Check if cleaning policy is correct
            stats = cache.get_stats()
            assert stats["conf"]["cleaning_policy"] == cp_from.value

            cache.set_cleaning_policy(cp_to.value)

            # Check if cleaning policy is correct
            stats = cache.get_stats()
            assert stats["conf"]["cleaning_policy"] == cp_to.value


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_cache_change_seq_cut_off_policy(pyocf_ctx, cm, cls):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=cm, cache_line_size=cls
    )

    # Create 2 core devices
    core_device1 = Volume(S.from_MiB(10))
    core1 = Core.using_device(core_device1, name="core1")
    core_device2 = Volume(S.from_MiB(10))
    core2 = Core.using_device(core_device2, name="core2")

    # Add cores
    cache.add_core(core1)
    cache.add_core(core2)

    # Check all possible seq cut off policy switches
    for seq_from in SeqCutOffPolicy:
        for seq_to in SeqCutOffPolicy:
            cache.set_seq_cut_off_policy(seq_from.value)

            # Check if seq cut off policy is correct
            stats = core1.get_stats()
            assert stats["seq_cutoff_policy"] == seq_from.value
            stats = core2.get_stats()
            assert stats["seq_cutoff_policy"] == seq_from.value

            cache.set_seq_cut_off_policy(seq_to.value)

            # Check if seq cut off policy is correct
            stats = core1.get_stats()
            assert stats["seq_cutoff_policy"] == seq_to.value
            stats = core2.get_stats()
            assert stats["seq_cutoff_policy"] == seq_to.value


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_core_change_seq_cut_off_policy(pyocf_ctx, cm, cls):
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=cm, cache_line_size=cls
    )

    # Create 2 core devices
    core_device1 = Volume(S.from_MiB(10))
    core1 = Core.using_device(core_device1, name="core1")
    core_device2 = Volume(S.from_MiB(10))
    core2 = Core.using_device(core_device2, name="core2")

    # Add cores
    cache.add_core(core1)
    cache.add_core(core2)

    # Check all possible seq cut off policy switches for first core
    for seq_from in SeqCutOffPolicy:
        for seq_to in SeqCutOffPolicy:
            core1.set_seq_cut_off_policy(seq_from.value)

            # Check if seq cut off policy of the first core is correct
            stats = core1.get_stats()
            assert stats["seq_cutoff_policy"] == seq_from.value

            # Check if seq cut off policy of the second core did not change
            stats = core2.get_stats()
            assert stats["seq_cutoff_policy"] == SeqCutOffPolicy.DEFAULT

            core1.set_seq_cut_off_policy(seq_to.value)

            # Check if seq cut off policy of the first core is correct
            stats = core1.get_stats()
            assert stats["seq_cutoff_policy"] == seq_to.value

            # Check if seq cut off policy of the second core did not change
            stats = core2.get_stats()
            assert stats["seq_cutoff_policy"] == SeqCutOffPolicy.DEFAULT
