#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import c_uint8, c_uint32, c_uint64, c_bool, c_int, Structure
from pyocf.types.stats.shared import _Stat


class _Inactive(Structure):
    _fields_ = [("occupancy", _Stat), ("clean", _Stat), ("dirty", _Stat)]


class _FallbackPt(Structure):
    _fields_ = [("error_counter", c_int), ("status", c_bool)]


class CacheInfo(Structure):
    _fields_ = [
        ("attached", c_bool),
        ("volume_type", c_uint8),
        ("state", c_uint8),
        ("size", c_uint32),
        ("inactive", _Inactive),
        ("occupancy", c_uint32),
        ("dirty", c_uint32),
        ("dirty_for", c_uint64),
        ("dirty_initial", c_uint32),
        ("cache_mode", c_uint32),
        ("fallback_pt", _FallbackPt),
        ("cleaning_policy", c_uint32),
        ("promotion_policy", c_uint32),
        ("cache_line_size", c_uint64),
        ("flushed", c_uint32),
        ("core_count", c_uint32),
        ("metadata_footprint", c_uint64),
        ("metadata_end_offset", c_uint32),
    ]
