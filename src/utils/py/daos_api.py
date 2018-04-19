#!/usr/bin/python
"""
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
"""
import ctypes
import traceback


class RankList(ctypes.Structure):
    """ For those DAOS calls that take a rank list """
    _fields_ = [("rl_ranks", ctypes.POINTER(ctypes.c_uint32)),
                ("rl_nr", ctypes.c_uint)]


class IOV(ctypes.Structure):
    _fields_ = [("iov_buf", ctypes.c_void_p),
                ("iov_buf_len", ctypes.c_size_t),
                ("iov_len", ctypes.c_size_t)]


class TargetInfo(ctypes.Structure):
    """ Represents info about a given target """
    _fields_ = [("ta_type", ctypes.c_uint),
                ("ta_state", ctypes.c_uint),
                ("ta_perf", ctypes.c_int),
                ("ta_space", ctypes.c_int)]


class Info(ctypes.Structure):
    """ Structure to represent information about a pool"""
    _fields_ = [("pi_uuid", ctypes.c_ubyte * 16),
                ("pi_ntargets", ctypes.c_uint32),
                ("pi_ndisabled", ctypes.c_uint32),
                ("pi_mode", ctypes.c_uint),
                ("pi_space", ctypes.c_int),
                ("pi_rebuild_st", ctypes.c_ubyte * 32)]


class DaosPool(object):
    """ A python object representing a DAOS pool."""

    def __init__(self, context):
        """ setup the python pool object, not the real pool. """
        self.context = context
        self.uuid = (ctypes.c_ubyte * 1)()
        self.group = ""
        self.handle = ctypes.c_uint64(0)
        self.glob = None
        self.svc = None
        self.pool_info = None
        self.target_info = None

    def create(self, mode, uid, gid, size, group):
        """ send a pool creation request to the daos server group """
        c_mode = ctypes.c_uint(mode)
        c_uid = ctypes.c_uint(uid)
        c_gid = ctypes.c_uint(gid)
        c_size = ctypes.c_longlong(size)
        c_group = ctypes.create_string_buffer(group)
        c_uuid = (ctypes.c_ubyte * 16)()
        rank = ctypes.c_uint(1)
        rl_ranks = ctypes.POINTER(ctypes.c_uint)(rank)
        c_whatever = ctypes.create_string_buffer(b"rubbish")

        svc = RankList(rl_ranks, 1)
        self.svc = svc

        func = self.context.get_function('create-pool')
        rc = func(c_mode, c_uid, c_gid, c_group, None, c_whatever, c_size,
                  ctypes.byref(svc), c_uuid, None)
        self.uuid = c_uuid
        self.group = group

        if rc != 0:
            raise ValueError("Pool create returned non-zero. RC: {0}"
                             .format(rc))

    def connect(self, flags):
        """ connect to this pool. """
        if not len(self.uuid) == 16:
            raise ValueError("No existing UUID for pool.")

        c_group = ctypes.create_string_buffer(self.group)
        c_flags = ctypes.c_uint(flags)
        c_handle = ctypes.c_longlong(0)
        c_info = Info()

        func = self.context.get_function('connect-pool')
        rc = func(self.uuid, c_group, ctypes.byref(self.svc), c_flags,
                  ctypes.byref(c_handle), ctypes.byref(c_info), None)
        self.handle = c_handle

        if rc != 0:
            raise ValueError("Pool connect returned non-zero. RC: {0}"
                             .format(rc))

    def disconnect(self):

        func = self.context.get_function('disconnect-pool')
        rc = func(self.handle, None)
        if rc != 0:
            raise ValueError("Pool disconnect returned non-zero. RC: {0}"
                             .format(rc))

    def local2global(self, poh):

        c_glob = IOV()
        func = self.context.get_function("local2global")
        rc = func(poh, ctypes.byref(c_glob))
        if rc != 0:
            raise ValueError("Pool local2global returned non-zero. RC: {0}"
                             .format(rc))
        return c_glob

    def global2local(self, glob):

        c_handle = ctypes.c_uint64(0)
        func = self.context.get_function("global2local")

        rc = func(glob, ctypes.byref(c_handle))
        if rc != 0:
            raise ValueError("Pool global2local returned non-zero. RC: {0}"
                             .format(rc))
        return c_handle

    def exclude(self, tgt_rank_list):
        """Exclude a set of storage targets from a pool."""

        rl_ranks = DaosPool.__pylist_to_array(tgt_rank_list)
        c_tgts = RankList(rl_ranks, len(tgt_rank_list))

        func = self.context.get_function('pool-exclude')
        rc = func(self.uuid, self.group, ctypes.byref(self.svc),
                  ctypes.byref(c_tgts), None)

        if rc != 0:
            raise ValueError("Pool exclude returned non-zero. RC: {0}"
                             .format(rc))

    def extend(self):
        """Extend the pool to more targets."""

        raise NotImplementedError("Extend not implemented in C API yet.")

    def evict(self):
        """Evict all connections to a pool."""

        func = self.context.get_function('pool-evict')
        rc = func(self.uuid, self.group, ctypes.byref(self.svc), None)
        if rc != 0:
            raise ValueError("Pool evict returned non-zero. RC: {0}".format(rc))

    def tgt_add(self, tgt_rank_list):
        """add a set of storage targets to a pool."""

        rl_ranks = DaosPool.__pylist_to_array(tgt_rank_list)
        c_tgts = RankList(rl_ranks, len(tgt_rank_list))

        func = self.context.get_function("target-add")
        rc = func(self.uuid, self.group, ctypes.byref(self.svc),
                  ctypes.byref(c_tgts), None)
        if rc != 0:
            raise ValueError("Pool tgt_add returned non-zero. RC: {0}"
                             .format(rc))

    def exclude_out(self, tgt_rank_list):
        """Exclude completely a set of storage targets from a pool."""

        rl_ranks = DaosPool.__pylist_to_array(tgt_rank_list)
        c_tgts = RankList(rl_ranks, len(tgt_rank_list))

        func = self.context.get_function('exclude-out')
        rc = func(self.uuid, self.group, ctypes.byref(self.svc),
                  ctypes.byref(c_tgts), None)
        if rc != 0:
            raise ValueError("Pool exclude_out returned non-zero. RC: {0}"
                             .format(rc))

    def pool_svc_stop(self):
        """Stop the current pool service leader."""

        func = self.context.get_function('service-stop')
        rc = func(self.handle, None)
        if rc != 0:
            raise ValueError("Pool svc_Stop returned non-zero. RC: {0}"
                             .format(rc))

    def pool_query(self):
        """Query pool information."""
        c_info = Info()

        func = self.context.get_function('pool-query')
        rc = func(self.handle, None, ctypes.byref(c_info), None)
        self.pool_info = c_info

        if rc != 0:
            raise ValueError("Pool query returned non-zero. RC: {0}"
                             .format(rc))
        return self.pool_info

    def target_query(self, tgt):
        """Query information of storage targets within a DAOS pool."""
        raise NotImplementedError("Target_query not yet implemented in C API.")

    def destroy(self, force):
        if not len(self.uuid) == 16:
            raise ValueError("No existing UUID for pool.")
        c_grp = ctypes.create_string_buffer(self.group)
        c_force = ctypes.c_uint(force)

        func = self.context.get_function('destroy-pool')
        rc = func(self.uuid, c_grp, c_force, None)
        if rc != 0:
            raise ValueError("Pool destroy returned non-zero. RC: {0}"
                             .format(rc))

    @staticmethod
    def __pylist_to_array(pylist):

        return (ctypes.c_uint32 * len(pylist))(*pylist)


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
             'create-pool': self.libdaos.daos_pool_create,
             'destroy-pool': self.libdaos.daos_pool_destroy,
             'kill-server': self.libdaos.daos_mgmt_svc_rip,
             'connect-pool': self.libdaos.daos_pool_connect,
             'disconnect-pool': self.libdaos.daos_pool_disconnect,
             'local2global': self.libdaos.daos_pool_local2global,
             'global2local': self.libdaos.daos_pool_global2local,
             'pool-exclude': self.libdaos.daos_pool_exclude,
             'pool-extend': self.libdaos.daos_pool_extend,
             'pool-evict': self.libdaos.daos_pool_evict,
             'target-add': self.libdaos.daos_pool_tgt_add,
             'exclude-out': self.libdaos.daos_pool_exclude_out,
             'service-stop': self.libdaos.daos_pool_svc_stop,
             'pool-query': self.libdaos.daos_pool_query,
             'target-query': self.libdaos.daos_pool_target_query,
        }

    def __del__(self):
        """ cleanup the DAOS API and MPI """
        self.libdaos.daos_fini()

    def get_function(self, function):
        """ call a function through the API """
        return self.ftable[function]
