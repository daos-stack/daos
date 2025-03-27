#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import os
import sys
from ctypes import (
    c_uint64,
    c_uint32,
    c_uint16,
    c_int
)
from tests.utils.random import RandomStringGenerator, RandomGenerator, DefaultRanges, Range

from pyocf.types.cache import CacheMode, MetadataLayout, PromotionPolicy
from pyocf.types.shared import CacheLineSize

import pytest

sys.path.append(os.path.join(os.path.dirname(__file__), os.path.pardir))


def enum_min(enum):
    return list(enum)[0].value


def enum_max(enum):
    return list(enum)[-1].value


def enum_range(enum):
    return Range(enum_min(enum), enum_max(enum))


@pytest.fixture(params=RandomGenerator(DefaultRanges.UINT16))
def c_uint16_randomize(request):
    return request.param


@pytest.fixture(params=RandomGenerator(DefaultRanges.UINT32))
def c_uint32_randomize(request):
    return request.param


@pytest.fixture(params=RandomGenerator(DefaultRanges.UINT64))
def c_uint64_randomize(request):
    return request.param


@pytest.fixture(params=RandomGenerator(DefaultRanges.INT))
def c_int_randomize(request):
    return request.param


@pytest.fixture(params=RandomGenerator(DefaultRanges.INT))
def c_int_sector_randomize(request):
    return request.param // 512 * 512


@pytest.fixture(params=RandomStringGenerator())
def string_randomize(request):
    return request.param


@pytest.fixture(
    params=RandomGenerator(DefaultRanges.UINT32).exclude_range(enum_range(CacheMode))
)
def not_cache_mode_randomize(request):
    return request.param


@pytest.fixture(
    params=RandomGenerator(DefaultRanges.UINT32).exclude_range(enum_range(CacheLineSize))
)
def not_cache_line_size_randomize(request):
    return request.param


@pytest.fixture(
    params=RandomGenerator(DefaultRanges.UINT32).exclude_range(enum_range(PromotionPolicy))
)
def not_promotion_policy_randomize(request):
    return request.param


@pytest.fixture(
    params=RandomGenerator(DefaultRanges.UINT32).exclude_range(enum_range(MetadataLayout))
)
def not_metadata_layout_randomize(request):
    return request.param
