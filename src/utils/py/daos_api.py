#!/usr/bin/python3
'''
  (C) Copyright 2018 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
import ctypes
import traceback

class RankList(ctypes.Structure):
    """ For those DAOS calls that take a rank list """
    _fields_ = [("rl_ranks", ctypes.POINTER(ctypes.c_uint)),
                ("rl_nr", ctypes.c_uint)]

class DaosPool(object):
    """ A python object representing a DAOS pool."""

    def __init__(self, context):
        """ setup the python pool object, not the real pool. """
        self.context = context
        self.uuid = ""

    def create(self, mode, uid, gid, size, group):
        """ send a pool creation request to the daos server group """
        c_mode = ctypes.c_uint(mode)
        c_uid = ctypes.c_uint(uid)
        c_gid = ctypes.c_uint(gid)
        c_size = ctypes.c_longlong(size)
        c_group = ctypes.create_string_buffer(group)
        c_uuid = (ctypes.c_int*3)()
        rank = ctypes.c_uint(1)
        rl_ranks = ctypes.POINTER(ctypes.c_uint)(rank)
        whatever = ctypes.create_string_buffer(b"rubbish")

        svc = RankList(rl_ranks, 1)

        func = self.context.get_function('create-pool')
        func(c_mode, c_uid, c_gid, c_group, None, whatever, c_size,
             ctypes.byref(svc), c_uuid, None)
        self.uuid = c_uuid

class DaosServer(object):
    """Represents a DAOS Server"""

    def __init__(self, context, group, rank):
        """ setup the python pool object, not the real pool. """
        self.context = context
        self.group_name = group
        self.rank = rank

    def kill(self, force):
        """ send a pool creation request to the daos server group """
        c_group = ctypes.create_string_buffer(self.group_name)
        c_force = ctypes.c_int(force)
        c_rank = ctypes.c_uint(self.rank)

        func = self.context.get_function('kill-server')
        func(c_group, c_rank, c_force, None)

class DaosContext(object):
    """Provides environment and other info for a DAOS client."""

    def __init__(self, path):
        """ setup the DAOS API and MPI """

        # I had to manually encode the loader mode because python
        # doesn't support all the values in dlfcn.h, also there is a
        # circular dependency in the DAOS libraries that mandates use
        # of LAZY symbol loading and the load order used below.
        ctypes.CDLL(path+"libdaos_common.so", mode=0x00101)
        self.libdaos = ctypes.CDLL(path+"libdaos.so.0.0.2", mode=0x00101)
        self.libdaos.daos_init()
        self.ftable = {
             'create-pool':self.libdaos.daos_pool_create,
             'kill-server':self.libdaos.daos_mgmt_svc_rip,
        }

    def __del__(self):
        """ cleanup the DAOS API and MPI """
        self.libdaos.daos_fini()

    def get_function(self, function):
        """ call a function through the API """
        return self.ftable[function]

if __name__ == '__main__':
    # test the API functions, this is just unit testing
    # for this file.

    try:
        CONTEXT = DaosContext('path to library goes here')
        print("initialized!!!\n")

        POOL = DaosPool(CONTEXT)
        POOL.create(448, 11374638, 11374638, 1024*1024*1024, b'daos_server')
        print ("created!\n")

        SERVICE = DaosServer(CONTEXT, b'daos_server', 0)
        SERVICE.kill(1)
        print ("server killed!\n")

    except Exception as EXCEP:
        print ("Something horrible happened\n")
        print (traceback.format_exc())
        print (EXCEP)
