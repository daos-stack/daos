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
import threading
import time
import uuid
import json
import random

# DAOS api C structures
class RankList(ctypes.Structure):
    """ For those DAOS calls that take a rank list """
    _fields_ = [("rl_ranks", ctypes.POINTER(ctypes.c_uint32)),
                ("rl_nr", ctypes.c_uint)]

class IOV(ctypes.Structure):
    _fields_ = [("iov_buf", ctypes.c_void_p),
                ("iov_buf_len", ctypes.c_size_t),
                ("iov_len", ctypes.c_size_t)]

class SGL(ctypes.Structure):
    _fields_ = [("sg_nr", ctypes.c_uint32),
                ("sg_nr_out", ctypes.c_uint32),
                ("sg_iovs", ctypes.POINTER(IOV))]

class EpochRange(ctypes.Structure):
    _fields_ = [("epr_lo", ctypes.c_uint64),
                ("epr_hi", ctypes.c_uint64)]

class TargetInfo(ctypes.Structure):
    """ Represents info about a given target """
    _fields_ = [("ta_type", ctypes.c_uint),
                ("ta_state", ctypes.c_uint),
                ("ta_perf", ctypes.c_int),
                ("ta_space", ctypes.c_int)]

class Info(ctypes.Structure):
    """ Structure to represent information about a pool """
    _fields_ = [("pi_uuid", ctypes.c_ubyte * 16),
                ("pi_ntargets", ctypes.c_uint32),
                ("pi_ndisabled", ctypes.c_uint32),
                ("pi_map_ver", ctypes.c_uint32),
                ("pi_uid", ctypes.c_uint32),
                ("pi_gid", ctypes.c_uint32),
                ("pi_mode", ctypes.c_uint32),
                ("pi_leader", ctypes.c_uint32),
                ("pi_space", ctypes.c_int),
                ("pi_rebuild_st", ctypes.c_ubyte * 32)]

class ContInfo(ctypes.Structure):
    """ Structure to represent information about a container """
    _fields_ = [("ci_uuid", ctypes.c_ubyte * 16),
                ("es_hce", ctypes.c_uint64),
                ("es_lre", ctypes.c_uint64),
                ("es_lhe", ctypes.c_uint64),
                ("es_ghce", ctypes.c_uint64),
                ("es_glre", ctypes.c_uint64),
                ("es_ghpce", ctypes.c_uint64),
                ("ci_nsnapshots", ctypes.c_uint32),
                ("ci_snapshots", ctypes.POINTER(ctypes.c_uint64)),
                ("ci_min_slipped_epoch", ctypes.c_uint64)]

class DaosEvent(ctypes.Structure):
    _fields_ = [("ev_error", ctypes.c_int),
                ("ev_private", ctypes.c_ulonglong * 19),
                ("ev_debug", ctypes.c_ulonglong)]

class DaosObjId(ctypes.Structure):
    _fields_ = [("lo", ctypes.c_uint64),
                ("hi", ctypes.c_uint64)]

class CheckSum(ctypes.Structure):
    _fields_ = [("cs_type", ctypes.c_uint),
                ("cs_len", ctypes.c_ushort),
                ("cs_buf_len", ctypes.c_ushort),
                ("cs_csum", ctypes.c_void_p)]

class DaosIODescriptor(ctypes.Structure):
    _fields_ = [("iod_name", IOV),
                ("iod_kcsum", CheckSum),
                ("iod_type", ctypes.c_int),
                ("iod_size", ctypes.c_uint64),
                ("iod_nr", ctypes.c_uint32),
                ("iod_recxs", ctypes.c_void_p),
                ("iod_csums", ctypes.POINTER(CheckSum)),
                ("iod_eprs", ctypes.c_void_p)]

def AsyncWorker1(func_ref, param_list, context, cb_func=None, obj=None):
    """ Wrapper function that calls the daos C code.  This can
        be used to run the DAOS library functions in a thread
        (or to just run them in the current thread too).

        func_ref   --which daos_api function to call
        param_list --parameters the c function takes
        context    --the API context object
        cb_func    --optional if caller wants notification of completion
        obj        --optional passed to the callback function

        This is done in a way that exercises the
        DAOS event code which is cumbersome and done more simply
        by other means. Its good for testing but replace this
        implementation if this is used as something other than a test
        tool.
    """
    # TODO insufficient error handling in this function

    # setup the asynchronous infrastructure the API requires
    the_event = param_list[-1]
    param_list[-1] = ctypes.byref(the_event)

    qfunc = context.get_function('create-eq')
    qhandle = ctypes.c_ulonglong(0)
    rc = qfunc(ctypes.byref(qhandle))

    efunc = context.get_function('init-event')
    rc = efunc(param_list[-1], qhandle, None)

    # calling the api function here
    rc = func_ref(*param_list)

    # use the API polling mechanism to tell when its done
    efunc = context.get_function('poll-eq')
    c_wait = ctypes.c_int(0)
    c_timeout = ctypes.c_ulonglong(-1)
    c_num = ctypes.c_uint(1)
    anotherEvent = DaosEvent()
    c_event_ptr = ctypes.pointer(anotherEvent)

    # start polling, wait forever
    rc = efunc(qhandle, c_wait, c_timeout, c_num, ctypes.byref(c_event_ptr))

    # signal the caller that api function has completed
    if cb_func is not None:
        cb_event = CallbackEvent(obj, the_event)
        cb_func(cb_event)

    # clean up
    qfunc = context.get_function('destroy-eq')
    qfunc(ctypes.byref(qhandle))

def AsyncWorker2(func_ref, param_list, context, cb_func=None, obj=None):
    """
        See AsyncWorker1 for details.  This does the same thing but
        uses different API functions (test instead of poll) for test
        coverage purposes.
    """
    # TODO insufficient error handling in this function

    # setup the asynchronous infrastructure the API requires
    the_event = param_list[-1]
    param_list[-1] = ctypes.byref(the_event)

    qfunc = context.get_function('create-eq')
    qhandle = ctypes.c_ulonglong(0)
    rc = qfunc(ctypes.byref(qhandle))

    efunc = context.get_function('init-event')
    rc = efunc(param_list[-1], qhandle, None)

    # call the api function
    rc = func_ref(*param_list)

    # -1 means wait forever
    c_timeout = ctypes.c_ulonglong(-1)

    c_event_ptr = ctypes.byref(the_event)
    efunc = context.get_function('test-event')
    c_flag = ctypes.c_bool(0)
    rc = efunc(c_event_ptr, c_timeout, ctypes.byref(c_flag));

    # signal caller API function has completed
    if cb_func is not None:
        cb_event = CallbackEvent(obj, anotherEvent)
        cb_func(cb_event)

    # cleanup
    qfunc = context.get_function('destroy-eq')
    qfunc(ctypes.byref(qhandle))

def create_oid(context):
    """ generate a random oid """
    func = context.get_function('generate-oid')
    func.restype = DaosObjId
    c_oid = func(1, 0, 0);
    return c_oid

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
    for i in range (0,31,2):
        c_uuid[i/2] = int(hexstr[i:i+2], 16)

# python API starts here
class CallbackEvent(object):
    def __init__(self, obj, event):
        self.obj = obj
        self.event = event

class DaosPool(object):
    """ A python object representing a DAOS pool."""

    def __init__(self, context):
        """ setup the python pool object, not the real pool. """
        self.attached = 0
        self.context = context
        self.uuid = (ctypes.c_ubyte * 1)()
        self.group = ctypes.create_string_buffer(b"not set")
        self.handle = ctypes.c_uint64(0)
        self.glob = None
        self.svc = None
        self.pool_info = None
        self.target_info = None

    def get_uuid_str(self):
        return c_uuid_to_str(self.uuid)

    def create(self, mode, uid, gid, size, group, target_list=None,
               cb_func=None):
        """ send a pool creation request to the daos server group """
        c_mode = ctypes.c_uint(mode)
        c_uid = ctypes.c_uint(uid)
        c_gid = ctypes.c_uint(gid)
        c_size = ctypes.c_longlong(size)
        if group is not None:
            self.group = ctypes.create_string_buffer(group)
        else:
            self.group = None
        self.uuid = (ctypes.c_ubyte * 16)()
        rank = ctypes.c_uint(1)
        rl_ranks = ctypes.POINTER(ctypes.c_uint)(rank)
        c_whatever = ctypes.create_string_buffer(b"rubbish")
        self.svc = RankList(rl_ranks, 1)

        # assuming for now target list is a server rank list
        if target_list is not None:
            tlist = DaosPool.__pylist_to_array(target_list)
            c_tgts = RankList(tlist, len(tlist))
            tgt_ptr = ctypes.byref(c_tgts)
        else:
            tgt_ptr = None

        func = self.context.get_function('create-pool')

        # the callback function is optional, if not supplied then run the
        # create synchronously, if its there then run it in a thread
        if cb_func == None:
            rc = func(c_mode, c_uid, c_gid, self.group, tgt_ptr,
                      c_whatever, c_size, ctypes.byref(self.svc),
                      self.uuid, None)
            if rc != 0:
                raise ValueError("Pool create returned non-zero. RC: {0}"
                                 .format(rc))
            else:
                self.attached = 1
        else:
            event = DaosEvent()
            params = [c_mode, c_uid, c_gid, self.group, tgt_ptr,
                      c_whatever, c_size,
                      ctypes.byref(self.svc), self.uuid, event]
            t = threading.Thread(target=AsyncWorker1,
                                 args=(func,params,self.context,cb_func,self))
            t.start()

    def connect(self, flags, cb_func=None):
        """ connect to this pool. """
        if not len(self.uuid) == 16:
            raise ValueError("No existing UUID for pool.")

        c_flags = ctypes.c_uint(flags)
        c_info = Info()
        func = self.context.get_function('connect-pool')

        # the callback function is optional, if not supplied then run the
        # create synchronously, if its there then run it in a thread
        if cb_func is None:
            rc = func(self.uuid, self.group, ctypes.byref(self.svc), c_flags,
                      ctypes.byref(self.handle), ctypes.byref(c_info), None)
            if rc != 0:
                raise ValueError("Pool connect returned non-zero. RC: {0}"
                                 .format(rc))
        else:
           event = DaosEvent()
           params = [self.uuid, self.group, ctypes.byref(self.svc), c_flags,
                      ctypes.byref(self.handle), ctypes.byref(c_info), event]
           t = threading.Thread(target=AsyncWorker1,
                                args=(func, params, self.context,
                                                            cb_func, self))
           t.start()

    def disconnect(self, cb_func=None):
        """ undoes the fine work done by the connect function above """

        func = self.context.get_function('disconnect-pool')
        if cb_func is None:
            rc = func(self.handle, None)
            if rc != 0:
                raise ValueError("Pool disconnect returned non-zero. RC: {0}"
                                 .format(rc))
        else:
            event = DaosEvent()
            params = [self.handle, event]
            t = threading.Thread(target=AsyncWorker1,
                                 args=(func,params,self.context,cb_func,self))
            t.start()

    def local2global(self, poh):
        """ Create a local pool connection for global representation data. """

        c_glob = IOV()
        func = self.context.get_function("convert-local")
        rc = func(poh, ctypes.byref(c_glob))
        if rc != 0:
            raise ValueError("Pool local2global returned non-zero. RC: {0}"
                             .format(rc))
        return c_glob

    def global2local(self, glob):

        c_handle = ctypes.c_uint64(0)
        func = self.context.get_function("convert-global")

        rc = func(glob, ctypes.byref(c_handle))
        if rc != 0:
            raise ValueError("Pool global2local returned non-zero. RC: {0}"
                             .format(rc))
        return c_handle

    def exclude(self, tgt_rank_list, cb_func=None):
        """Exclude a set of storage targets from a pool."""

        rl_ranks = DaosPool.__pylist_to_array(tgt_rank_list)
        c_tgts = RankList(rl_ranks, len(tgt_rank_list))

        func = self.context.get_function('exclude-target')
        if cb_func is None:
            rc = func(self.uuid, self.group, ctypes.byref(self.svc),
                      ctypes.byref(c_tgts), None)
            if rc != 0:
                raise ValueError("Pool exclude returned non-zero. RC: {0}"
                             .format(rc))
        else:
            event = DaosEvent()
            params = [self.uuid, self.group, ctypes.byref(self.svc),
                      ctypes.byref(c_tgts), event]
            t = threading.Thread(target=AsyncWorker1, args=(func,params,
                      self.context,cb_func,self))
            t.start()

    def extend(self):
        """Extend the pool to more targets."""

        raise NotImplementedError("Extend not implemented in C API yet.")

    def evict(self, cb_func=None):
        """Evict all connections to a pool."""

        func = self.context.get_function('evict-client')

        if cb_func is None:
            rc = func(self.uuid, self.group, ctypes.byref(self.svc), None)
            if rc != 0:
                raise ValueError(
                    "Pool evict returned non-zero. RC: {0}".format(rc))
        else:
            event = DaosEvent()
            params = [self.uuid, self.group, ctypes.byref(self.svc), event]
            t = threading.Thread(target=AsyncWorker1, args=(func,params,
                      self.context,cb_func,self))
            t.start()

    def tgt_add(self, tgt_rank_list, cb_func=None):
        """add a set of storage targets to a pool."""

        rl_ranks = DaosPool.__pylist_to_array(tgt_rank_list)
        c_tgts = RankList(rl_ranks, len(tgt_rank_list))
        func = self.context.get_function("add-target")

        if cb_func is None:
            rc = func(self.uuid, self.group, ctypes.byref(self.svc),
                      ctypes.byref(c_tgts), None)
            if rc != 0:
                raise ValueError("Pool tgt_add returned non-zero. RC: {0}"
                             .format(rc))
        else:
            event = DaosEvent()
            params = [self.uuid, self.group, ctypes.byref(self.svc),
                      ctypes.byref(c_tgts), event]
            t = threading.Thread(target=AsyncWorker1, args=(func, params,
                      self.context, cb_func, self))
            t.start()

    def exclude_out(self, tgt_rank_list, cb_func=None):
        """Exclude completely a set of storage targets from a pool."""

        rl_ranks = DaosPool.__pylist_to_array(tgt_rank_list)
        c_tgts = RankList(rl_ranks, len(tgt_rank_list))
        func = self.context.get_function('kill-target')

        if cb_func is None:
            rc = func(self.uuid, self.group, ctypes.byref(self.svc),
                      ctypes.byref(c_tgts), None)
            if rc != 0:
                raise ValueError("Pool exclude_out returned non-zero. RC: {0}"
                                 .format(rc))
        else:
            event = DaosEvent()
            params = [self.uuid, self.group, ctypes.byref(self.svc),
                      ctypes.byref(c_tgts), event]
            t = threading.Thread(target=AsyncWorker1, args=(func, params,
                      self.context, cb_func, self))
            t.start()

    def pool_svc_stop(self, cb_func=None):
        """Stop the current pool service leader."""

        func = self.context.get_function('service-stop')

        if cb_func is None:
            rc = func(self.handle, None)
            if rc != 0:
                raise ValueError("Pool svc_Stop returned non-zero. RC: {0}"
                                 .format(rc))
        else:
            event = DaosEvent()
            params = [self.handle, event]
            t = threading.Thread(target=AsyncWorker1, args=(func, params,
                      self.context, cb_func, self))
            t.start()

    def pool_query(self, cb_func=None):
        """Query pool information."""

        self.pool_info = Info()
        func = self.context.get_function('query-pool')

        if cb_func is None:
            rc = func(self.handle, None, ctypes.byref(self.pool_info), None)
            if rc != 0:
                raise ValueError("Pool query returned non-zero. RC: {0}"
                                 .format(rc))
            return self.pool_info
        else:
            event = DaosEvent()
            params = [self.handle, None, ctypes.byref(self.pool_info), event]
            t = threading.Thread(target=AsyncWorker1, args=(func, params,
                      self.context, cb_func, self))
            t.start()
        return None

    def target_query(self, tgt):
        """Query information of storage targets within a DAOS pool."""
        raise NotImplementedError("Target_query not yet implemented in C API.")

    def destroy(self, force, cb_func=None):

        if not len(self.uuid) == 16 or self.attached == 0:
            raise ValueError("No existing UUID for pool.")

        c_force = ctypes.c_uint(force)
        func = self.context.get_function('destroy-pool')

        if cb_func is None:
            rc = func(self.uuid, self.group, c_force, None)
            if rc != 0:
                raise ValueError("Pool destroy returned non-zero. RC: {0}"
                                 .format(rc))
            else:
                self.attached = 0
        else:
            event = DaosEvent()
            params = [self.uuid, self.group, c_force, event]

            t = threading.Thread(target=AsyncWorker1, args=(func, params,
                      self.context, cb_func, self))
            t.start()

    @staticmethod
    def __pylist_to_array(pylist):

        return (ctypes.c_uint32 * len(pylist))(*pylist)


class IORequest(object):
    """ Python object that centralizes details about an I/O """

    def __init__(self, context, coh, oid):
        self.context = context
        self.coh = coh

        if oid is None:
            # create a new object
            self.oid = create_oid(self.context)
        else:
            self.oid = oid

        # 1 is DAOS_IOD_SINGLE from daos_types.h
        self.io_type = ctypes.c_int(1)

        self.sgl = SGL()

        self.iod = DaosIODescriptor()
        ctypes.memset(ctypes.byref(self.iod.iod_kcsum), 0, 16)

        self.epoch_range = EpochRange()
        self.oh = ctypes.c_uint64(0)

        cs = CheckSum()
        cs.cs_sum = ctypes.pointer(ctypes.create_string_buffer(32))
        cs.cs_buf_len = 32
        cs.cs_len = 0
        self.iod.iod_csums = ctypes.pointer(cs)

        func = self.context.get_function('open-obj')

        c_epoch = ctypes.c_uint64(0)
        c_mode = ctypes.c_uint(4)
        rc = func(self.coh, self.oid, c_epoch, c_mode, ctypes.byref(self.oh),
                  None)
        if rc != 0:
            raise ValueError("Object open returned non-zero. RC: {0}"
                             .format(rc))

    def __del__(self):
        """ cleanup this request """
        func = self.context.get_function('close-obj')
        rc = func(self.oh, None)
        if rc != 0:
            raise ValueError("Object close returned non-zero. RC: {0}"
                             .format(rc))

    def single_insert(self, dkey, akey, value, size, epoch):

        # put the data into the scatter gather list
        sgl_iov = IOV()
        sgl_iov.iov_len = size
        sgl_iov.iov_buf_len = size
        sgl_iov.iov_buf = ctypes.cast(value, ctypes.c_void_p)
        self.sgl.sg_iovs = ctypes.pointer(sgl_iov)
        self.sgl.sg_nr = 1
        self.sgl.sg_nr_out = 1

        self.epoch_range.epr_lo = epoch
        self.epoch_range.epr_hi = ~0

        # setup the descriptor
        self.iod.iod_name.iov_buf = ctypes.cast(akey, ctypes.c_void_p)
        self.iod.iod_name.iov_buf_len = ctypes.sizeof(akey)
        self.iod.iod_name.iov_len = ctypes.sizeof(akey)
        self.iod.iod_type = 1
        self.iod.iod_size = size
        self.iod.iod_nr = 1
        self.iod.iod_eprs = ctypes.cast(ctypes.pointer(self.epoch_range),
                                        ctypes.c_void_p)

        # now do it
        func = self.context.get_function('update-obj')

        dkey_iov = IOV()
        dkey_iov.iov_buf = ctypes.cast(dkey, ctypes.c_void_p)
        dkey_iov.iov_buf_len = ctypes.sizeof(dkey)
        dkey_iov.iov_len = ctypes.sizeof(dkey)

        rc = func(self.oh, self.epoch_range.epr_lo, ctypes.byref(dkey_iov),
                  self.iod.iod_nr,
                  ctypes.byref(self.iod), ctypes.byref(self.sgl), None)
        if rc != 0:
            raise ValueError("Object update returned non-zero. RC: {0}"
                             .format(rc))

    def single_fetch(self, dkey, akey, size, epoch):

        sgl_iov = IOV()
        sgl_iov.iov_len = ctypes.c_size_t(size)
        sgl_iov.iov_buf_len = ctypes.c_size_t(size)

        #buf = (ctypes.c_ubyte * size)(0)
        buf = ctypes.create_string_buffer(size)
        sgl_iov.iov_buf = ctypes.cast(buf, ctypes.c_void_p)
        self.sgl.sg_iovs = ctypes.pointer(sgl_iov)
        self.sgl.sg_nr = 1
        self.sgl.sg_nr_out = 1

        self.epoch_range.epr_lo = epoch
        self.epoch_range.epr_hi = ~0

        # setup the descriptor
        self.iod.iod_name.iov_buf = ctypes.cast(akey, ctypes.c_void_p)
        self.iod.iod_name.iov_buf_len = ctypes.sizeof(akey)
        self.iod.iod_name.iov_len = ctypes.sizeof(akey)
        self.iod.iod_type = 1
        self.iod.iod_size = ctypes.c_size_t(size)
        self.iod.iod_nr = 1
        self.iod.iod_eprs = ctypes.cast(ctypes.pointer(self.epoch_range),
                                        ctypes.c_void_p)

        # now do it
        func = self.context.get_function('fetch-obj')

        dkey_iov = IOV()
        dkey_iov.iov_buf = ctypes.cast(dkey, ctypes.c_void_p)
        dkey_iov.iov_buf_len = ctypes.sizeof(dkey)
        dkey_iov.iov_len = ctypes.sizeof(dkey)

        rc = func(self.oh, self.epoch_range.epr_lo, ctypes.byref(dkey_iov),
                  self.iod.iod_nr,
                  ctypes.byref(self.iod), ctypes.byref(self.sgl), None, None)
        if rc != 0:
            raise ValueError("Object fetch returned non-zero. RC: {0}"
                             .format(rc))
        return buf


class DaosContainer(object):
    """ A python object representing a DAOS container."""


    def __init__(self, context, cuuid=None, poh=None, coh=None):
        """ setup the python container object, not the real container. """
        self.context = context

        # ignoring caller parameters for now

        self.uuid = (ctypes.c_ubyte * 1)()
        self.coh = ctypes.c_uint64(0)
        self.poh = ctypes.c_uint64(0)
        self.info = ContInfo()

    def get_uuid_str(self):
        return c_uuid_to_str(self.uuid)

    def create(self, poh, con_uuid=None, cb_func=None):
        """ send a container creation request to the daos server group """

        # create a random uuid if none is provided
        self.uuid = (ctypes.c_ubyte * 16)()
        if con_uuid is None:
            c_uuid(uuid.uuid4(), self.uuid)
        else:
            c_uuid(con_uuid, self.uuid)

        self.poh = poh

        func = self.context.get_function('create-cont')

        # the callback function is optional, if not supplied then run the
        # create synchronously, if its there then run it in a thread
        if cb_func == None:
            rc = func(self.poh, self.uuid, None)
            if rc != 0:
                raise ValueError("Container create returned non-zero. RC: {0}"
                                 .format(rc))
        else:
            event = DaosEvent()
            params = [self.poh, self.uuid, event]
            t = threading.Thread(target=AsyncWorker1,
                                 args=(func,params,self.context,cb_func,self))
            t.start()

    def destroy(self, force=1, poh=None, con_uuid=None, cb_func=None):
        """ send a container destroy request to the daos server group """

        # caller can override pool handle and uuid
        if poh is not None:
            self.poh = poh
        if con_uuid is not None:
            c_uuid(con_uuid, self.uuid)

        c_force = ctypes.c_uint(force)

        func = self.context.get_function('destroy-cont')

        # the callback function is optional, if not supplied then run the
        # create synchronously, if its there then run it in a thread
        if cb_func == None:
            rc = func(self.poh, self.uuid, c_force, None)
            if rc != 0:
                raise ValueError("Pool create returned non-zero. RC: {0}"
                                 .format(rc))
        else:
            event = DaosEvent()
            params = [self.poh, self.uuid, c_force, event]
            t = threading.Thread(target=AsyncWorker1,
                                 args=(func,params,self.context,cb_func,self))
            t.start()

    def open(self, poh=None, cuuid=None, flags=None, cb_func=None):
        """ send a container open request to the daos server group """

        # parameters can be used to associate this python object with a
        # DAOS container or they may already have been set
        if poh is not None:
            self.poh = poh
        if cuuid is not None:
            c_uuid(cuuid, self.uuid)

        # Note that 2 is read/write
        c_flags = ctypes.c_uint(2)
        if flags is not None:
            c_flags = ctypes.c_uint(flags)

        func = self.context.get_function('open-cont')

        # the callback function is optional, if not supplied then run the
        # create synchronously, if its there then run it in a thread
        if cb_func == None:
            rc = func(self.poh, self.uuid, c_flags, ctypes.byref(self.coh),
                      ctypes.byref(self.info), None)
            if rc != 0:
                raise ValueError("container open returned non-zero. RC: {0}"
                                 .format(rc))
        else:
            event = DaosEvent()
            params = [self.poh, self.uuid, c_flags, ctypes.byref(self.coh),
                      ctypes.byref(c_info), event]
            t = threading.Thread(target=AsyncWorker1,
                                 args=(func,params,self.context,cb_func,self))
            t.start()

    def close(self, coh=None, cb_func=None):
        """ send a container close request to the daos server group """

        # parameters can be used to associate this python object with a
        # DAOS container or they may already have been set
        if coh is not None:
            self.coh = coh

        func = self.context.get_function('close-cont')

        # the callback function is optional, if not supplied then run the
        # create synchronously, if its there then run it in a thread
        if cb_func == None:
            rc = func(self.coh, None)
            if rc != 0:
                raise ValueError("container close returned non-zero. RC: {0}"
                                 .format(rc))
        else:
            event = DaosEvent()
            params = [self.coh, event]
            t = threading.Thread(target=AsyncWorker1,
                                 args=(func,params,self.context,cb_func,self))
            t.start()

    def query(self, coh=None, cb_func=None):
        """Query container information."""

        # allow caller to override the handle
        if coh is not None:
            self.coh = coh

        func = self.context.get_function('query-cont')

        if cb_func is None:
            rc = func(self.coh, ctypes.byref(self.info), None)
            if rc != 0:
                raise ValueError("Container query returned non-zero. RC: {0}"
                                 .format(rc))
            return self.info
        else:
            event = DaosEvent()
            params = [self.coh, ctypes.byref(self.info), event]
            t = threading.Thread(target=AsyncWorker1, args=(func, params,
                      self.context, cb_func, self))
            t.start()
        return None

    def get_new_epoch(self):
        """ get the next epoch for this container """

        # container should be  in the open state
        if self.coh == 0:
            raise ValueError("Container needs to be open.")

        epoch = 0
        c_epoch = ctypes.c_uint64(epoch)

        func = self.context.get_function('get-epoch')
        rc = func(self.coh, ctypes.byref(c_epoch), None, None)
        if rc != 0:
            raise ValueError("Epoch hold returned non-zero. RC: {0}"
                             .format(rc))
        return c_epoch.value;

    def commit_epoch(self, epoch):
        """ close out an epoch that is done being modified """

        # container should be  in the open state
        if self.coh == 0:
            raise ValueError("Container needs to be open.")

        func = self.context.get_function('commit-epoch')
        rc = func(self.coh, epoch, None, None)
        if rc != 0:
            raise ValueError("Epoch commit returned non-zero. RC: {0}"
                             .format(rc))

    def write_an_obj(self, thedata, size, dkey, akey, oid=None):
        """ create a really simple obj in this container and commit """

        # container should be  in the open state
        if self.coh == 0:
            raise ValueError("Container needs to be open.")

        epoch = self.get_new_epoch()

        c_value = ctypes.create_string_buffer(thedata)
        c_size = ctypes.c_size_t(size)
        c_dkey = ctypes.create_string_buffer(dkey)
        c_akey = ctypes.create_string_buffer(akey)
        c_epoch = ctypes.c_uint64(epoch)

        # oid can be None in which case a new one is created
        ioreq = IORequest(self.context, self.coh, oid)
        ioreq.single_insert(c_dkey, c_akey, c_value, c_size, c_epoch)

        self.commit_epoch(c_epoch)
        return ioreq.oid, c_epoch.value

    def read_an_obj(self, size, dkey, akey, oid, epoch):
        """ create a really simple obj in this container and commit """

        # container should be  in the open state
        if self.coh == 0:
            raise ValueError("Container needs to be open.")

        c_size = ctypes.c_size_t(size)
        c_dkey = ctypes.create_string_buffer(dkey)
        c_akey = ctypes.create_string_buffer(akey)
        c_epoch = ctypes.c_uint64(epoch)

        ioreq = IORequest(self.context, self.coh, oid)
        buf = ioreq.single_fetch(c_dkey, c_akey, size, c_epoch)
        return buf

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
        rc = func(c_group, c_rank, c_force, None)
        if rc != 0:
            raise ValueError("Server kill returned non-zero. RC: {0}"
                             .format(rc))

class DaosContext(object):
    """Provides environment and other info for a DAOS client."""

    def __init__(self, path):
        """ setup the DAOS API and MPI """

        self.libdaos = ctypes.CDLL(path+"libdaos.so.0.0.2",
                                   mode=ctypes.DEFAULT_MODE)
        ctypes.CDLL(path+"libdaos_common.so",
                    mode=ctypes.RTLD_GLOBAL)

        self.libtest = ctypes.CDLL(path+"libdaos_tests.so",
                                   mode=ctypes.DEFAULT_MODE)
        self.libdaos.daos_init()
        # Note: action-subject format
        self.ftable = {
             'add-target'     : self.libdaos.daos_pool_tgt_add,
             'close-cont'     : self.libdaos.daos_cont_close,
             'close-obj'      : self.libdaos.daos_obj_close,
             'commit-epoch'   : self.libdaos.daos_epoch_commit,
             'connect-pool'   : self.libdaos.daos_pool_connect,
             'convert-global' : self.libdaos.daos_pool_global2local,
             'covert-local'   : self.libdaos.daos_pool_local2global,
             'create-cont'    : self.libdaos.daos_cont_create,
             'create-pool'    : self.libdaos.daos_pool_create,
             'create-eq'      : self.libdaos.daos_eq_create,
             'destroy-cont'   : self.libdaos.daos_cont_destroy,
             'destroy-pool'   : self.libdaos.daos_pool_destroy,
             'destroy-eq'     : self.libdaos.daos_eq_destroy,
             'disconnect-pool': self.libdaos.daos_pool_disconnect,
             'evict-client'   : self.libdaos.daos_pool_evict,
             'exclude-target' : self.libdaos.daos_pool_exclude,
             'extend-pool'    : self.libdaos.daos_pool_extend,
             'fetch-obj'      : self.libdaos.daos_obj_fetch,
             'generate-oid'   : self.libtest.dts_oid_gen,
             'get-epoch'      : self.libdaos.daos_epoch_hold,
             'init-event'     : self.libdaos.daos_event_init,
             'generate-oid'   : self.libtest.dts_oid_gen,
             'kill-server'    : self.libdaos.daos_mgmt_svc_rip,
             'kill-target'    : self.libdaos.daos_pool_exclude_out,
             'open-cont'      : self.libdaos.daos_cont_open,
             'open-obj'       : self.libdaos.daos_obj_open,
             'poll-eq'        : self.libdaos.daos_eq_poll,
             'query-cont'     : self.libdaos.daos_cont_query,
             'query-pool'     : self.libdaos.daos_pool_query,
             'query-target'   : self.libdaos.daos_pool_target_query,
             'stop-service'   : self.libdaos.daos_pool_svc_stop,
             'test-event'     : self.libdaos.daos_event_test,
             'update-obj'     : self.libdaos.daos_obj_update
        }

    def __del__(self):
        """ cleanup the DAOS API """
        self.libdaos.daos_fini()

    def get_function(self, function):
        """ call a function through the API """
        return self.ftable[function]

if __name__ == '__main__':
    # this file is not intended to be run in normal circumstances
    # this is strictly unit test code here in main, there is a lot
    # of rubbish but it makes it easy to try stuff out as we expand
    # this interface.  Will eventially be removed or formalized.

    try:
        # this works so long as this file is in its usual place
        with open('../../../.build_vars.json') as f:
            data = json.load(f)

        CONTEXT = DaosContext(data['PREFIX'] + '/lib/')
        print("initialized!!!\n")

        POOL = DaosPool(CONTEXT)
        tgt_list=[1]
        POOL.create(448, 11374638, 11374638, 1024 * 1024 * 1024,
                    b'daos_server')
        time.sleep(2)
        print ("Pool create called\n")
        print ("uuid is " + POOL.get_uuid_str())

        #time.sleep(5)
        print ("handle before connect {0}\n".format(POOL.handle))

        POOL.connect(1 << 1)

        print ("Main: handle after connect {0}\n".format(POOL.handle))

        CONTAINER = DaosContainer(CONTEXT)
        CONTAINER.create(POOL.handle)

        print("container created {}".format(CONTAINER.get_uuid_str()))

        #POOL.pool_svc_stop();
        #POOL.pool_query()

        time.sleep(5)

        CONTAINER.open()
        print("container opened {}".format(CONTAINER.get_uuid_str()))

        time.sleep(5)

        CONTAINER.query()
        print("Epoch highest committed: {}".format(CONTAINER.info.es_hce))

        thedata = "a string that I want to stuff into an object"
        size = 45
        dkey = "this is the dkey"
        akey = "this is the akey"

        oid, epoch = CONTAINER.write_an_obj(thedata, size, dkey, akey)
        print("data write finished with epoch {}".format(epoch))

        time.sleep(5)

        thedata2 = CONTAINER.read_an_obj(size, dkey, akey, oid, epoch)
        print(repr(thedata2.value))

        thedata3 = "a different string that I want to stuff into an object"
        size = 55
        dkey2 = "this is the dkey"
        akey2 = "this is the akey"

        oid2, epoch2 = CONTAINER.write_an_obj(thedata3, size, dkey2, akey2, oid)
        print("data write finished, in epoch {}".format(epoch2))

        time.sleep(5)

        thedata4 = CONTAINER.read_an_obj(size, dkey2, akey2, oid2, epoch2)
        print(repr(thedata4.value))

        thedata5 = CONTAINER.read_an_obj(size, dkey2, akey2, oid, epoch)
        print(repr(thedata5.value))

        CONTAINER.close()
        print("container closed {}".format(CONTAINER.get_uuid_str()))

        time.sleep(15)

        CONTAINER.destroy(1)

        print("container destroyed ")

        #POOL.disconnect(rubbish)
        #POOL.disconnect()
        #print ("Main past disconnect\n");

        #time.sleep(5)

        #tgts = [2]
        #POOL.exclude(tgts, rubbish)
        #POOL.exclude_out(tgts, rubbish)
        #POOL.exclude_out(tgts)
        #print ("Main past exclude\n");

        #POOL.evict(rubbish)

        #time.sleep(5)

        #POOL.tgt_add(tgts, rubbish)

        #print ("Main past tgt_add\n");

        #POOL.destroy(1)
        #print("Pool destroyed")

        #SERVICE = DaosServer(CONTEXT, b'daos_server', 5)
        #SERVICE.kill(1)
        #print ("server killed!\n")

    except Exception as EXCEP:
        print ("Something horrible happened\n")
        print (traceback.format_exc())
        print (EXCEP)
