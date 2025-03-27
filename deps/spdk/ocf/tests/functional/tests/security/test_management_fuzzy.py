#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import pytest
import string

from pyocf.types.cache import (
    Cache,
    CacheMode,
    CACHE_MODE_NONE,
    CleaningPolicy,
    AlruParams,
    AcpParams,
    PromotionPolicy,
    NhitParams,
    ConfValidValues,
)
from pyocf.types.core import Core
from pyocf.types.volume import Volume
from pyocf.utils import Size as S
from tests.utils.random import (
    Range,
    RandomGenerator,
    DefaultRanges,
    RandomStringGenerator,
)
from pyocf.types.shared import OcfError, CacheLineSize, SeqCutOffPolicy
from ctypes import c_uint64, c_uint32, c_uint8


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_change_cache_mode(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to change cache mode to invalid value.
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Change cache mode to invalid one and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in [item.value for item in CacheMode]:
            continue
        with pytest.raises(OcfError, match="Error changing cache mode"):
            cache.change_cache_mode(i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_cleaning_policy(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to change cleaning policy to invalid value
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Set cleaning policy to invalid one and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in [item.value for item in CleaningPolicy]:
            continue
        with pytest.raises(OcfError, match="Error changing cleaning policy"):
            cache.set_cleaning_policy(i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_attach_cls(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to change cache line size to
    invalid value while attaching cache device
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache(owner=cache_device.owner, cache_mode=cm, cache_line_size=cls)
    cache.start_cache()

    # Check whether it is possible to attach cache device with invalid cache line size
    for i in RandomGenerator(DefaultRanges.UINT64):
        if i in [item.value for item in CacheLineSize]:
            continue
        with pytest.raises(OcfError, match="Attaching cache device failed"):
            cache.attach_device(cache_device, cache_line_size=i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_cache_set_seq_cut_off_policy(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to change cache seq cut-off policy to invalid value
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Create 2 core devices
    core_device1 = Volume(S.from_MiB(10))
    core1 = Core.using_device(core_device1, name="core1")
    core_device2 = Volume(S.from_MiB(10))
    core2 = Core.using_device(core_device2, name="core2")

    # Add cores
    cache.add_core(core1)
    cache.add_core(core2)

    # Change cache seq cut off policy to invalid one and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in [item.value for item in SeqCutOffPolicy]:
            continue
        with pytest.raises(OcfError, match="Error setting cache seq cut off policy"):
            cache.set_seq_cut_off_policy(i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_cache_set_seq_cut_off_promotion(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to change cache seq cut-off promotion count to invalid value
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Create 2 core devices
    core_device1 = Volume(S.from_MiB(10))
    core1 = Core.using_device(core_device1, name="core1")
    core_device2 = Volume(S.from_MiB(10))
    core2 = Core.using_device(core_device2, name="core2")

    # Add cores
    cache.add_core(core1)
    cache.add_core(core2)

    # Change cache seq cut off promotion count to invalid one and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in ConfValidValues.seq_cutoff_promotion_range:
            continue
        with pytest.raises(
            OcfError, match="Error setting cache seq cut off policy promotion count"
        ):
            cache.set_seq_cut_off_promotion(i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_core_set_seq_cut_off_promotion(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to change core seq cut-off promotion count to invalid value
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Create core device
    core_device1 = Volume(S.from_MiB(10))
    core1 = Core.using_device(core_device1, name="core1")

    # Add core
    cache.add_core(core1)

    # Change core seq cut off promotion count to invalid one and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in ConfValidValues.seq_cutoff_promotion_range:
            continue
        with pytest.raises(
            OcfError, match="Error setting core seq cut off policy promotion count"
        ):
            core1.set_seq_cut_off_promotion(i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_cache_set_seq_cut_off_threshold(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to change cache seq cut-off threshold to invalid value
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Create 2 core devices
    core_device1 = Volume(S.from_MiB(10))
    core1 = Core.using_device(core_device1, name="core1")
    core_device2 = Volume(S.from_MiB(10))
    core2 = Core.using_device(core_device2, name="core2")

    # Add cores
    cache.add_core(core1)
    cache.add_core(core2)

    # Change cache seq cut off policy to invalid one and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in ConfValidValues.seq_cutoff_threshold_rage:
            continue
        with pytest.raises(
            OcfError, match="Error setting cache seq cut off policy threshold"
        ):
            cache.set_seq_cut_off_threshold(i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_core_set_seq_cut_off_threshold(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to change core seq cut-off threshold to invalid value
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Create core device
    core_device = Volume(S.from_MiB(10))
    core = Core.using_device(core_device, name="core")

    # Add core
    cache.add_core(core)

    # Change core seq cut off policy to invalid one and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in ConfValidValues.seq_cutoff_threshold_rage:
            continue
        with pytest.raises(
            OcfError, match="Error setting core seq cut off policy threshold"
        ):
            core.set_seq_cut_off_threshold(i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_core_set_seq_cut_off_policy(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to change core seq cut-off policy to invalid value
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Create core device
    core_device = Volume(S.from_MiB(10))
    core = Core.using_device(core_device)

    # Add core
    cache.add_core(core)

    # Change core seq cut off policy to invalid one and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in [item.value for item in SeqCutOffPolicy]:
            continue
        with pytest.raises(OcfError, match="Error setting core seq cut off policy"):
            core.set_seq_cut_off_policy(i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_alru_param(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to set invalid param for alru cleaning policy
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Change invalid alru param and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in [item.value for item in AlruParams]:
            continue
        with pytest.raises(OcfError, match="Error setting cleaning policy param"):
            cache.set_cleaning_policy_param(CleaningPolicy.ALRU, i, 1)
            print(f"\n{i}")


def get_alru_param_valid_rage(param_id):
    if param_id == AlruParams.WAKE_UP_TIME:
        return ConfValidValues.cleaning_alru_wake_up_time_range
    elif param_id == AlruParams.STALE_BUFFER_TIME:
        return ConfValidValues.cleaning_alru_staleness_time_range
    elif param_id == AlruParams.FLUSH_MAX_BUFFERS:
        return ConfValidValues.cleaning_alru_flush_max_buffers_range
    elif param_id == AlruParams.ACTIVITY_THRESHOLD:
        return ConfValidValues.cleaning_alru_activity_threshold_range


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.parametrize("param", AlruParams)
@pytest.mark.security
def test_neg_set_alru_param_value(pyocf_ctx, cm, cls, param):
    """
    Test whether it is possible to set invalid value to any of alru cleaning policy params
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :param param: alru parameter to fuzz
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    cache.set_cleaning_policy(CleaningPolicy.ALRU)

    # Set to invalid alru param value and check if failed
    valid_range = get_alru_param_valid_rage(param)
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in valid_range:
            continue
        with pytest.raises(OcfError, match="Error setting cleaning policy param"):
            cache.set_cleaning_policy_param(CleaningPolicy.ALRU, param, i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_acp_param(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to set invalid param for acp cleaning policy
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Change invalid acp param and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in [item.value for item in AcpParams]:
            continue
        with pytest.raises(OcfError, match="Error setting cleaning policy param"):
            cache.set_cleaning_policy_param(CleaningPolicy.ACP, i, 1)
            print(f"\n{i}")


def get_acp_param_valid_rage(param_id):
    if param_id == AcpParams.WAKE_UP_TIME:
        return ConfValidValues.cleaning_acp_wake_up_time_range
    elif param_id == AcpParams.FLUSH_MAX_BUFFERS:
        return ConfValidValues.cleaning_acp_flush_max_buffers_range


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.parametrize("param", AcpParams)
@pytest.mark.security
def test_neg_set_acp_param_value(pyocf_ctx, cm, cls, param):
    """
    Test whether it is possible to set invalid value to any of acp cleaning policy params
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :param param: acp parameter to fuzz
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    cache.set_cleaning_policy(CleaningPolicy.ACP)

    # Set to invalid acp param value and check if failed
    valid_range = get_acp_param_valid_rage(param)
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in valid_range:
            continue
        with pytest.raises(OcfError, match="Error setting cleaning policy param"):
            cache.set_cleaning_policy_param(CleaningPolicy.ACP, param, i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_promotion_policy(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to set invalid param for promotion policy
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Change to invalid promotion policy and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in [item.value for item in PromotionPolicy]:
            continue
        with pytest.raises(OcfError, match="Error setting promotion policy"):
            cache.set_promotion_policy(i)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_nhit_promotion_policy_param(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to set invalid promotion policy param id for nhit promotion policy
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device,
        cache_mode=cm,
        cache_line_size=cls,
        promotion_policy=PromotionPolicy.NHIT,
    )

    # Set invalid promotion policy param id and check if failed
    for i in RandomGenerator(DefaultRanges.UINT8):
        if i in [item.value for item in NhitParams]:
            continue
        with pytest.raises(OcfError, match="Error setting promotion policy parameter"):
            cache.set_promotion_policy_param(PromotionPolicy.NHIT, i, 1)
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_nhit_promotion_policy_param_trigger(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to set invalid promotion policy param TRIGGER_THRESHOLD for
    nhit promotion policy
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device,
        cache_mode=cm,
        cache_line_size=cls,
        promotion_policy=PromotionPolicy.NHIT,
    )

    # Set to invalid promotion policy trigger threshold and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in ConfValidValues.promotion_nhit_trigger_threshold_range:
            continue
        with pytest.raises(OcfError, match="Error setting promotion policy parameter"):
            cache.set_promotion_policy_param(
                PromotionPolicy.NHIT, NhitParams.TRIGGER_THRESHOLD, i
            )
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_nhit_promotion_policy_param_threshold(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to set invalid promotion policy param INSERTION_THRESHOLD for
    nhit promotion policy
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device,
        cache_mode=cm,
        cache_line_size=cls,
        promotion_policy=PromotionPolicy.NHIT,
    )

    # Set to invalid promotion policy insertion threshold and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in ConfValidValues.promotion_nhit_insertion_threshold_range:
            continue
        with pytest.raises(OcfError, match="Error setting promotion policy parameter"):
            cache.set_promotion_policy_param(
                PromotionPolicy.NHIT, NhitParams.INSERTION_THRESHOLD, i
            )
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_ioclass_max_size(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to add ioclass with invaild max size
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Set invalid max size and check if failed
    for i in RandomGenerator(DefaultRanges.UINT32):
        if i in ConfValidValues.ioclass_max_size_range:
            continue
        with pytest.raises(OcfError, match="Error adding partition to cache"):
            cache.configure_partition(
                part_id=1,
                name="unclassified",
                max_size=i,
                priority=0,
                cache_mode=CACHE_MODE_NONE,
            )
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_ioclass_priority(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to add ioclass with invaild priority
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Set invalid priority and check if failed
    for i in RandomGenerator(DefaultRanges.INT16):
        if i in ConfValidValues.ioclass_priority_range:
            continue
        with pytest.raises(OcfError, match="Error adding partition to cache"):
            cache.configure_partition(
                part_id=1,
                name="unclassified",
                max_size=100,
                priority=i,
                cache_mode=CACHE_MODE_NONE,
            )
            print(f"\n{i}")


@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.security
def test_neg_set_ioclass_cache_mode(pyocf_ctx, cm, cls):
    """
    Test whether it is possible to add ioclass with invaild cache mode
    :param pyocf_ctx: basic pyocf context fixture
    :param cm: cache mode we start with
    :param cls: cache line size we start with
    :return:
    """
    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(cache_device, cache_mode=cm, cache_line_size=cls)

    # Set invalid cache mode and check if failed
    for i in RandomGenerator(DefaultRanges.INT):
        if i in list(CacheMode) + [CACHE_MODE_NONE]:
            continue
        with pytest.raises(OcfError, match="Error adding partition to cache"):
            cache.configure_partition(
                part_id=1, name="unclassified", max_size=100, priority=1, cache_mode=i
            )
            print(f"\n{i}")


@pytest.mark.security
def test_neg_set_ioclass_name(pyocf_ctx):
    """
    Test whether it is possible to add ioclass with invaild name
    :param pyocf_ctx: basic pyocf context fixture
    :return:
    """
    invalid_chars = [chr(c) for c in range(256) if chr(c) not in string.printable]
    invalid_chars += [",", '"']

    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=CacheMode.WT, cache_line_size=CacheLineSize.LINE_4KiB
    )

    # Set invalid name and check if failed
    for name in RandomStringGenerator(
        len_range=Range(0, 1024), count=10000, extra_chars=invalid_chars
    ):
        if not any(c for c in invalid_chars if c in name):
            continue
        with pytest.raises(OcfError, match="Error adding partition to cache"):
            cache.configure_partition(part_id=1, name=name, max_size=100, priority=1)
            print(f"\n{name}")


@pytest.mark.security
def test_neg_set_ioclass_name_len(pyocf_ctx):
    """
    Test whether it is possible to add ioclass with too long name
    :param pyocf_ctx: basic pyocf context fixture
    :return:
    """

    # Start cache device
    cache_device = Volume(S.from_MiB(30))
    cache = Cache.start_on_device(
        cache_device, cache_mode=CacheMode.WT, cache_line_size=CacheLineSize.LINE_4KiB
    )

    # Set invalid name and check if failed
    for name in RandomStringGenerator(len_range=Range(1025, 4096), count=10000):
        with pytest.raises(OcfError, match="Error adding partition to cache"):
            cache.configure_partition(part_id=1, name=name, max_size=100, priority=1)
            print(f"\n{name}")
