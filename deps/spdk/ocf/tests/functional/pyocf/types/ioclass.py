#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import c_uint8, c_uint32, c_int, c_int16, c_uint16, c_char, c_char_p, Structure


class IoClassInfo(Structure):
    MAX_IO_CLASS_NAME_SIZE = 1024
    _fields_ = [
        ("_name", c_char * MAX_IO_CLASS_NAME_SIZE),
        ("_cache_mode", c_int),
        ("_priority", c_int16),
        ("_curr_size", c_uint32),
        ("_min_size", c_uint32),
        ("_max_size", c_uint32),
        ("_cleaning_policy_type", c_int),
    ]


class IoClassConfig(Structure):
    _fields_ = [
        ("_class_id", c_uint32),
        ("_max_size", c_uint32),
        ("_name", c_char_p),
        ("_cache_mode", c_int),
        ("_prio", c_uint16),
    ]


class IoClassesInfo(Structure):
    MAX_IO_CLASSES = 33
    _fields_ = [("_config", IoClassConfig * MAX_IO_CLASSES)]
