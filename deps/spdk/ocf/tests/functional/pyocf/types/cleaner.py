#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import c_void_p, CFUNCTYPE, Structure, c_int
from .shared import SharedOcfObject


class CleanerOps(Structure):
    INIT = CFUNCTYPE(c_int, c_void_p)
    KICK = CFUNCTYPE(None, c_void_p)
    STOP = CFUNCTYPE(None, c_void_p)

    _fields_ = [("init", INIT), ("kick", KICK), ("stop", STOP)]


class Cleaner(SharedOcfObject):
    _instances_ = {}
    _fields_ = [("cleaner", c_void_p)]

    def __init__(self):
        self._as_parameter_ = self.cleaner
        super().__init__()

    @classmethod
    def get_ops(cls):
        return CleanerOps(init=cls._init, kick=cls._kick, stop=cls._stop)

    @staticmethod
    @CleanerOps.INIT
    def _init(cleaner):
        return 0

    @staticmethod
    @CleanerOps.KICK
    def _kick(cleaner):
        pass

    @staticmethod
    @CleanerOps.STOP
    def _stop(cleaner):
        pass
