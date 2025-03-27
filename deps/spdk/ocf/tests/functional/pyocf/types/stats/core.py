
#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import c_uint32, c_uint64, Structure

from .shared import OcfStatsReq, OcfStatsBlock, OcfStatsDebug, OcfStatsError


class CoreInfo(Structure):
    _fields_ = [
        ("core_size", c_uint64),
        ("core_size_bytes", c_uint64),
        ("dirty", c_uint32),
        ("flushed", c_uint32),
        ("dirty_for", c_uint64),
        ("seq_cutoff_threshold", c_uint32),
        ("seq_cutoff_policy", c_uint32),
    ]
