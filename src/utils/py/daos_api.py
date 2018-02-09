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
    _fields_ = [("num", ctypes.c_uint),
                ("num_out", ctypes.c_uint),
                ("rl_ranks", ctypes.POINTER(ctypes.c_uint))]

class DaosPool(object):
    """ A python object representing a DAOS pool."""

    def __init__(self, context):
        """ setup the python pool object, not the real pool. """
        self.context = context
        self.uuid = ""

    def create(self, mode, uid, gid, size, group):
        """ send a pool creation request to the daos server group """
        mode = ctypes.c_uint(mode)
        uid = ctypes.c_uint(uid)
        gid = ctypes.c_uint(gid)
        size = ctypes.c_longlong(size)
        group = ctypes.create_string_buffer(group)
        uuid = ctypes.create_string_buffer(17)
        rl_ranks = ctypes.POINTER(ctypes.c_uint)(ctypes.c_uint(0))
        whatever = ctypes.create_string_buffer(b"rubbish")

        svc = RankList(1, 0, rl_ranks)

        func = self.context.get_function('create-pool')
        func(mode, uid, gid, group, None, whatever, size,
             ctypes.byref(svc), uuid, None)
        self.uuid = uuid

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
        ctypes.CDLL(path+"libdaos_tier.so", mode=0x00101)
        self.libdaos.daos_init()
        self.ftable = {
             'create-pool':self.libdaos.daos_pool_create,
        }

    def __del__(self):
        """ cleanup the DAOS API and MPI """
        self.libdaos.daos_fini()

    def get_function(self, function):
        """ call a function through the API """
        return self.ftable[function]

if __name__ == '__main__':
    # test the API functions

    try:
        CONTEXT = DaosContext('/home/skirvan/daos_py/install/lib/')
        print("initialized!!!\n")

        POOL = DaosPool(CONTEXT)
        POOL.create(511, 0, 0, 1024*1024*1024, b'daos_server')
        print ("created\n")
    except Exception as EXCEP:
        print ("Something horrible happened\n")
        print (traceback.format_exc())
        print (EXCEP)
