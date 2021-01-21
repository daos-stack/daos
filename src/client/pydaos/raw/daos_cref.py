#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-few-public-methods
# pylint: disable=pylint-missing-docstring
import ctypes

# DAOS api C structures
class RankList(ctypes.Structure):
    """ For those DAOS calls that take a rank list """
    _fields_ = [("rl_ranks", ctypes.POINTER(ctypes.c_uint32)),
                ("rl_nr", ctypes.c_uint)]

class DTgtList(ctypes.Structure):
    """ Structure to represent rank/target list for target """
    _fields_ = [("tl_ranks", ctypes.POINTER(ctypes.c_uint32)),
                ("tl_tgts", ctypes.POINTER(ctypes.c_int32)),
                ("tl_nr", ctypes.c_uint32)]

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

class RebuildStatus(ctypes.Structure):
    """ Structure to represent rebuild status info """
    _fields_ = [("rs_version", ctypes.c_uint32),
                ("rs_seconds", ctypes.c_uint32),
                ("rs_errno", ctypes.c_uint32),
                ("rs_done", ctypes.c_uint32),
                ("rs_padding32", ctypes.c_uint32),
                ("rs_fail_rank", ctypes.c_uint32),
                ("rs_toberb_obj_nr", ctypes.c_uint64),
                ("rs_obj_nr", ctypes.c_uint64),
                ("rs_rec_nr", ctypes.c_uint64),
                ("rs_size", ctypes.c_uint64)]

class Daos_handle_t(ctypes.Structure):
    """ Structure to represent rebuild status info """
    _fields_ = [("cookie", ctypes.c_uint64)]

class Daos_Space(ctypes.Structure):
    """ Structure to represent Pool Target Space usage info """
    _fields_ = [("s_total", ctypes.c_uint64 * 2),
                ("s_free", ctypes.c_uint64 * 2)]

class PoolSpace(ctypes.Structure):
    """ Structure to represent Pool space usage info """
    _fields_ = [("ps_space", Daos_Space),
                ("ps_free_min", ctypes.c_uint64 * 2),
                ("ps_free_max", ctypes.c_uint64 * 2),
                ("ps_free_mean", ctypes.c_uint64 * 2),
                ("ps_ntargets", ctypes.c_uint32),
                ("ps_padding", ctypes.c_uint32)]

class PoolInfo(ctypes.Structure):
    """ Structure to represent information about a pool """
    _fields_ = [("pi_uuid", ctypes.c_ubyte * 16),
                ("pi_ntargets", ctypes.c_uint32),
                ("pi_nnodes", ctypes.c_uint32),
                ("pi_ndisabled", ctypes.c_uint32),
                ("pi_map_ver", ctypes.c_uint32),
                ("pi_leader", ctypes.c_uint32),
                ("pi_bits", ctypes.c_uint64),
                ("pi_space", PoolSpace),
                ("pi_rebuild_st", RebuildStatus)]


class DaosPropertyEntry(ctypes.Structure):
    _fields_ = [("dpe_type", ctypes.c_uint32),
                ("dpe_reserv", ctypes.c_uint32),
                ("dpe_val", ctypes.c_uint64)]


class DaosProperty(ctypes.Structure):
    _fields_ = [("dpp_nr", ctypes.c_uint32),
                ("dpp_reserv", ctypes.c_uint32),
                ("dpp_entries", ctypes.POINTER(DaosPropertyEntry))]

    def __init__(self, num_structs):
        super(DaosProperty, self).__init__()
        total_prop_entries = (DaosPropertyEntry * num_structs)()
        self.dpp_entries = ctypes.cast(total_prop_entries,
                                       ctypes.POINTER(DaosPropertyEntry))
        self.dpp_nr = num_structs
        self.dpp_reserv = 0
        for num in range(0, num_structs):
            self.dpp_entries[num].dpe_type = ctypes.c_uint32(0)
            self.dpp_entries[num].dpe_reserv = ctypes.c_uint32(0)
            self.dpp_entries[num].dpe_val = ctypes.c_uint64(0)


class ContInfo(ctypes.Structure):
    """ Structure to represent information about a container """
    _fields_ = [("ci_uuid", ctypes.c_ubyte * 16),
                ("es_hce", ctypes.c_uint64),
                ("es_lre", ctypes.c_uint64),
                ("es_lhe", ctypes.c_uint64),
                ("es_ghce", ctypes.c_uint64),
                ("es_glre", ctypes.c_uint64),
                ("es_ghpce", ctypes.c_uint64),
                ("ci_redun_fac", ctypes.c_uint32),
                ("ci_nsnapshots", ctypes.c_uint32),
                ("ci_snapshots", ctypes.POINTER(ctypes.c_uint64)),
                ("ci_min_slipped_epoch", ctypes.c_uint64)]

class DaosEvent(ctypes.Structure):
    _fields_ = [("ev_error", ctypes.c_int),
                ("ev_private", ctypes.c_ulonglong * 19),
                ("ev_debug", ctypes.c_ulonglong)]

class DaosObjClassAttr(ctypes.Structure):
    _fields_ = [("ca_schema", ctypes.c_int),
                ("ca_resil_degree", ctypes.c_int),
                ("ca_resil", ctypes.c_int),
                ("ca_grp_nr", ctypes.c_uint),
                ("u", ctypes.c_uint * 2)]

class DaosObjAttr(ctypes.Structure):
    _fields_ = [("oa_rank", ctypes.c_int),
                ("oa_oa", DaosObjClassAttr)]

class DaosObjId(ctypes.Structure):
    _fields_ = [("lo", ctypes.c_uint64),
                ("hi", ctypes.c_uint64)]

# Note hard-coded number of ranks, might eventually be a problem
class DaosObjShard(ctypes.Structure):
    _fields_ = [("os_replica_nr", ctypes.c_uint32),
                ("os_ranks", ctypes.c_uint32 * 5)]

# note the hard-coded number of ranks, might eventually be a problem
class DaosObjLayout(ctypes.Structure):
    _fields_ = [("ol_ver", ctypes.c_uint32),
                ("ol_class", ctypes.c_uint32),
                ("ol_nr", ctypes.c_uint32),
                ("ol_shards", ctypes.POINTER(DaosObjShard * 5))]

class Extent(ctypes.Structure):
    _fields_ = [("rx_idx", ctypes.c_uint64),
                ("rx_nr", ctypes.c_uint64)]

class DaosIODescriptor(ctypes.Structure):
    _fields_ = [("iod_name", IOV),
                ("iod_type", ctypes.c_int),
                ("iod_size", ctypes.c_uint64),
                ("iod_flags", ctypes.c_uint64),
                ("iod_nr", ctypes.c_uint32),
                ("iod_recxs", ctypes.POINTER(Extent))]

class Anchor(ctypes.Structure):
    """ Class to represent a C daos_anchor_t struct. """
    _fields_ = [('da_type', ctypes.c_uint16),
                ('da_shard', ctypes.c_uint16),
                ('da_padding', ctypes.c_uint32),
                ('da_buff', ctypes.c_uint8*128)]

class CallbackEvent(object):
    def __init__(self, obj, event):
        self.obj = obj
        self.event = event

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
    # TO be Done insufficient error handling in this function

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
    # TO be Done insufficient error handling in this function

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
    rc = efunc(c_event_ptr, c_timeout, ctypes.byref(c_flag))

    # signal caller API function has completed
    if cb_func is not None:
        cb_event = CallbackEvent(obj, anotherEvent)
        cb_func(cb_event)

    # cleanup
    qfunc = context.get_function('destroy-eq')
    qfunc(ctypes.byref(qhandle))

class Logfac:
    DEBUG = 0
    INFO = 1
    WARNING = 2
    ERROR = 3

#Transaction handle to update for an independent transaction
DAOS_TX_NONE = Daos_handle_t(0)
