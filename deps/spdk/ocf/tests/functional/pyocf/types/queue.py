#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import c_void_p, CFUNCTYPE, Structure, byref
from threading import Thread, Condition, Event
import weakref

from ..ocf import OcfLib
from .shared import OcfError


class QueueOps(Structure):
    KICK = CFUNCTYPE(None, c_void_p)
    KICK_SYNC = CFUNCTYPE(None, c_void_p)
    STOP = CFUNCTYPE(None, c_void_p)

    _fields_ = [("kick", KICK), ("kick_sync", KICK_SYNC), ("stop", STOP)]


class Queue:
    pass


def io_queue_run(*, queue: Queue, kick: Condition, stop: Event):
    def wait_predicate():
        return stop.is_set() or OcfLib.getInstance().ocf_queue_pending_io(queue)

    while True:
        with kick:
            kick.wait_for(wait_predicate)

        OcfLib.getInstance().ocf_queue_run(queue)

        if stop.is_set() and not OcfLib.getInstance().ocf_queue_pending_io(queue):
            break


class Queue:
    _instances_ = {}

    def __init__(self, cache, name):

        self.ops = QueueOps(kick=type(self)._kick, stop=type(self)._stop)

        self.handle = c_void_p()
        status = OcfLib.getInstance().ocf_queue_create(
            cache.cache_handle, byref(self.handle), byref(self.ops)
        )
        if status:
            raise OcfError("Couldn't create queue object", status)

        Queue._instances_[self.handle.value] = weakref.ref(self)
        self._as_parameter_ = self.handle

        self.stop_event = Event()
        self.kick_condition = Condition()
        self.thread = Thread(
            group=None,
            target=io_queue_run,
            name=name,
            kwargs={
                "queue": self,
                "kick": self.kick_condition,
                "stop": self.stop_event,
            },
        )
        self.thread.start()

    @classmethod
    def get_instance(cls, ref):
        return cls._instances_[ref]()

    @staticmethod
    @QueueOps.KICK_SYNC
    def _kick_sync(ref):
        Queue.get_instance(ref).kick_sync()

    @staticmethod
    @QueueOps.KICK
    def _kick(ref):
        Queue.get_instance(ref).kick()

    @staticmethod
    @QueueOps.STOP
    def _stop(ref):
        Queue.get_instance(ref).stop()

    def kick_sync(self):
        OcfLib.getInstance().ocf_queue_run(self.handle)

    def kick(self):
        with self.kick_condition:
            self.kick_condition.notify_all()

    def put(self):
        OcfLib.getInstance().ocf_queue_put(self)

    def stop(self):
        with self.kick_condition:
            self.stop_event.set()
            self.kick_condition.notify_all()

        self.thread.join()
