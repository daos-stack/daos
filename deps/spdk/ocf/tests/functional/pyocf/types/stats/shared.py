#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import c_uint64, c_uint32, Structure


class _Stat(Structure):
    _fields_ = [("value", c_uint64), ("fraction", c_uint64)]


class OcfStatsReq(Structure):
    _fields_ = [
        ("partial_miss", c_uint64),
        ("full_miss", c_uint64),
        ("total", c_uint64),
        ("pass_through", c_uint64),
    ]


class OcfStatsBlock(Structure):
    _fields_ = [("read", c_uint64), ("write", c_uint64)]


class OcfStatsError(Structure):
    _fields_ = [("read", c_uint32), ("write", c_uint32)]


class OcfStatsDebug(Structure):
    _fields_ = [
        ("read_size", c_uint64 * 12),
        ("write_size", c_uint64 * 12),
        ("read_align", c_uint64 * 4),
        ("write_align", c_uint64 * 4),
    ]


class UsageStats(Structure):
    _fields_ = [
        ("occupancy", _Stat),
        ("free", _Stat),
        ("clean", _Stat),
        ("dirty", _Stat),
    ]


class RequestsStats(Structure):
    _fields_ = [
        ("rd_hits", _Stat),
        ("rd_partial_misses", _Stat),
        ("rd_full_misses", _Stat),
        ("rd_total", _Stat),
        ("wr_hits", _Stat),
        ("wr_partial_misses", _Stat),
        ("wr_full_misses", _Stat),
        ("wr_total", _Stat),
        ("rd_pt", _Stat),
        ("wr_pt", _Stat),
        ("serviced", _Stat),
        ("total", _Stat),
    ]


class BlocksStats(Structure):
    _fields_ = [
        ("core_volume_rd", _Stat),
        ("core_volume_wr", _Stat),
        ("core_volume_total", _Stat),
        ("cache_volume_rd", _Stat),
        ("cache_volume_wr", _Stat),
        ("cache_volume_total", _Stat),
        ("volume_rd", _Stat),
        ("volume_wr", _Stat),
        ("volume_total", _Stat),
    ]


class ErrorsStats(Structure):
    _fields_ = [
        ("core_volume_rd", _Stat),
        ("core_volume_wr", _Stat),
        ("core_volume_total", _Stat),
        ("cache_volume_rd", _Stat),
        ("cache_volume_wr", _Stat),
        ("cache_volume_total", _Stat),
        ("total", _Stat),
    ]
