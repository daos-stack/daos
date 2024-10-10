"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=consider-using-f-string

import ctypes
import uuid


def c_uuid_to_str(cuuid):
    """ utility function to convert a C uuid into a standard string format """
    return '{:02X}{:02X}{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}-{:02X}' \
           '{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}'.format(*cuuid)


def c_uuid(puuid, cuuid):
    """ utility function to create a UUID in C format from a python UUID """
    hexstr = puuid.hex
    for index in range(0, 31, 2):
        cuuid[int(index / 2)] = int(hexstr[index:index + 2], 16)


def str_to_c_uuid(uuidstr):
    """ utility function to convert string format uuid to a C uuid """
    uuidstr2 = '{' + uuidstr + '}'
    puuid = uuid.UUID(uuidstr2)
    cuuid = (ctypes.c_ubyte * 16)()
    c_uuid(puuid, cuuid)
    return cuuid
