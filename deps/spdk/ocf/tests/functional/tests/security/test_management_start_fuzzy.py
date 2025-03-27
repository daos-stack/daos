#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import logging

import pytest

from pyocf.types.cache import Cache, CacheMode, MetadataLayout, PromotionPolicy
from pyocf.types.shared import OcfError, CacheLineSize
from pyocf.types.volume import Volume
from pyocf.utils import Size
from tests.utils.random import RandomGenerator, DefaultRanges, Range

logger = logging.getLogger(__name__)


def try_start_cache(**config):
    cache_device = Volume(Size.from_MiB(30))
    cache = Cache.start_on_device(cache_device, **config)
    cache.stop()

@pytest.mark.security
@pytest.mark.parametrize("cls", CacheLineSize)
def test_fuzzy_start_cache_mode(pyocf_ctx, cls, not_cache_mode_randomize):
    """
    Test whether it is impossible to start cache with invalid cache mode value.
    :param pyocf_ctx: basic pyocf context fixture
    :param cls: cache line size value to start cache with
    :param c_uint32_randomize: cache mode enum value to start cache with
    """
    with pytest.raises(OcfError, match="OCF_ERR_INVALID_CACHE_MODE"):
        try_start_cache(cache_mode=not_cache_mode_randomize, cache_line_size=cls)


@pytest.mark.security
@pytest.mark.parametrize("cm", CacheMode)
def test_fuzzy_start_cache_line_size(pyocf_ctx, not_cache_line_size_randomize, cm):
    """
    Test whether it is impossible to start cache with invalid cache line size value.
    :param pyocf_ctx: basic pyocf context fixture
    :param c_uint64_randomize: cache line size enum value to start cache with
    :param cm: cache mode value to start cache with
    """
    with pytest.raises(OcfError, match="OCF_ERR_INVALID_CACHE_LINE_SIZE"):
        try_start_cache(cache_mode=cm, cache_line_size=not_cache_line_size_randomize)


@pytest.mark.security
@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_fuzzy_start_name(pyocf_ctx, string_randomize, cm, cls):
    """
    Test whether it is possible to start cache with various cache name value.
    :param pyocf_ctx: basic pyocf context fixture
    :param string_randomize: fuzzed cache name value to start cache with
    :param cm: cache mode value to start cache with
    :param cls: cache line size value to start cache with
    """
    cache_device = Volume(Size.from_MiB(30))
    incorrect_values = ['']
    try:
        cache = Cache.start_on_device(cache_device, name=string_randomize, cache_mode=cm,
                                      cache_line_size=cls)
    except OcfError:
        if string_randomize not in incorrect_values:
            logger.error(
                f"Cache did not start properly with correct name value: '{string_randomize}'")
        return
    if string_randomize in incorrect_values:
        logger.error(f"Cache started with incorrect name value: '{string_randomize}'")
    cache.stop()


@pytest.mark.security
@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_fuzzy_start_metadata_layout(pyocf_ctx, not_metadata_layout_randomize, cm, cls):
    """
    Test whether it is impossible to start cache with invalid metadata layout value.
    :param pyocf_ctx: basic pyocf context fixture
    :param c_uint32_randomize: metadata layout enum value to start cache with
    :param cm: cache mode value to start cache with
    :param cls: cache line size value to start cache with
    """
    with pytest.raises(OcfError, match="OCF_ERR_INVAL"):
        try_start_cache(
            metadata_layout=not_metadata_layout_randomize,
            cache_mode=cm,
            cache_line_size=cls
        )


@pytest.mark.security
@pytest.mark.parametrize("cls", CacheLineSize)
@pytest.mark.parametrize('max_wb_queue_size', RandomGenerator(DefaultRanges.UINT32, 10))
def test_fuzzy_start_max_queue_size(pyocf_ctx, max_wb_queue_size, c_uint32_randomize, cls):
    """
    Test whether it is impossible to start cache with invalid dependence between max queue size
    and queue unblock size.
    :param pyocf_ctx: basic pyocf context fixture
    :param max_wb_queue_size: max queue size value to start cache with
    :param c_uint32_randomize: queue unblock size value to start cache with
    :param cls: cache line size value to start cache with
    """
    if c_uint32_randomize > max_wb_queue_size:
        with pytest.raises(OcfError, match="OCF_ERR_INVAL"):
            try_start_cache(
                max_queue_size=max_wb_queue_size,
                queue_unblock_size=c_uint32_randomize,
                cache_mode=CacheMode.WB,
                cache_line_size=cls)
    else:
        logger.warning(f"Test skipped for valid values: "
                       f"'max_queue_size={max_wb_queue_size}, "
                       f"queue_unblock_size={c_uint32_randomize}'.")


@pytest.mark.security
@pytest.mark.parametrize("cm", CacheMode)
@pytest.mark.parametrize("cls", CacheLineSize)
def test_fuzzy_start_promotion_policy(pyocf_ctx, not_promotion_policy_randomize, cm, cls):
    """
    Test whether it is impossible to start cache with invalid promotion policy
    :param pyocf_ctx: basic pyocf context fixture
    :param c_uint32_randomize: promotion policy to start with
    :param cm: cache mode value to start cache with
    :param cls: cache line size to start cache with
    """
    with pytest.raises(OcfError, match="OCF_ERR_INVAL"):
        try_start_cache(
            cache_mode=cm,
            cache_line_size=cls,
            promotion_policy=not_promotion_policy_randomize
        )
