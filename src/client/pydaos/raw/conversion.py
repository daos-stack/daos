#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import ctypes
import uuid

def c_uuid_to_str(uuid):
    """ utility function to convert a C uuid into a standard string format """
    uuid_str = '{:02X}{:02X}{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}-{:02X}'\
               '{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}'.format(
                   uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5],
                   uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
                   uuid[12], uuid[13], uuid[14], uuid[15])
    return uuid_str

def c_uuid(p_uuid, c_uuid):
    """ utility function to create a UUID in C format from a python UUID """
    hexstr = p_uuid.hex
    for i in range(0, 31, 2):
        c_uuid[int(i/2)] = int(hexstr[i:i+2], 16)

def str_to_c_uuid(uuidstr):
    """ utility function to convert string format uuid to a C uuid """
    uuidstr2 = '{' + uuidstr + '}'
    puuid = uuid.UUID(uuidstr2)
    cuuid = (ctypes.c_ubyte * 16)()
    c_uuid(puuid, cuuid)
    return cuuid
