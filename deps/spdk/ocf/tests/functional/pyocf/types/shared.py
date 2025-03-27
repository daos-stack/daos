#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import logging
from ctypes import CFUNCTYPE, c_size_t, c_char_p, Structure, c_void_p
from enum import IntEnum, auto
from threading import Event

from ..utils import Size as S


class OcfErrorCode(IntEnum):
    OCF_ERR_INVAL = 1000000
    OCF_ERR_AGAIN = auto()
    OCF_ERR_INTR = auto()
    OCF_ERR_NOT_SUPP = auto()
    OCF_ERR_NO_MEM = auto()
    OCF_ERR_NO_LOCK = auto()
    OCF_ERR_METADATA_VER = auto()
    OCF_ERR_NO_METADATA = auto()
    OCF_ERR_METADATA_FOUND = auto()
    OCF_ERR_INVAL_VOLUME_TYPE = auto()
    OCF_ERR_UNKNOWN = auto()
    OCF_ERR_TOO_MANY_CACHES = auto()
    OCF_ERR_NO_FREE_RAM = auto()
    OCF_ERR_START_CACHE_FAIL = auto()
    OCF_ERR_CACHE_NOT_EXIST = auto()
    OCF_ERR_CORE_NOT_EXIST = auto()
    OCF_ERR_CACHE_EXIST = auto()
    OCF_ERR_CORE_EXIST = auto()
    OCF_ERR_TOO_MANY_CORES = auto()
    OCF_ERR_CORE_NOT_AVAIL = auto()
    OCF_ERR_NOT_OPEN_EXC = auto()
    OCF_ERR_CACHE_NOT_AVAIL = auto()
    OCF_ERR_IO_CLASS_NOT_EXIST = auto()
    OCF_ERR_IO = auto()
    OCF_ERR_WRITE_CACHE = auto()
    OCF_ERR_WRITE_CORE = auto()
    OCF_ERR_DIRTY_SHUTDOWN = auto()
    OCF_ERR_DIRTY_EXISTS = auto()
    OCF_ERR_FLUSHING_INTERRUPTED = auto()
    OCF_ERR_FLUSH_IN_PROGRESS = auto()
    OCF_ERR_CANNOT_ADD_CORE_TO_POOL = auto()
    OCF_ERR_CACHE_IN_INCOMPLETE_STATE = auto()
    OCF_ERR_CORE_IN_INACTIVE_STATE = auto()
    OCF_ERR_INVALID_CACHE_MODE = auto()
    OCF_ERR_INVALID_CACHE_LINE_SIZE = auto()
    OCF_ERR_CACHE_NAME_MISMATCH = auto()
    OCF_ERR_INVAL_CACHE_DEV = auto()


class OcfCompletion:
    """
    This class provides Completion mechanism for interacting with OCF async
    management API.
    """

    class CompletionResult:
        def __init__(self, completion_args):
            self.completion_args = {
                x[0]: i for i, x in enumerate(completion_args)
            }
            self.results = None
            self.arg_types = [x[1] for x in completion_args]

        def __getitem__(self, key):
            try:
                position = self.completion_args[key]
                return self.results[position]
            except KeyError:
                raise KeyError(f"No completion argument {key} specified")

    def __init__(self, completion_args: list, context=None):
        """
        Provide ctypes arg list, and optionally index of status argument in
        completion function which will be extracted (default - last argument).

        :param completion_args: list of tuples (parameter name, parameter type)
            for OCF completion function
        """
        self.e = Event()
        self.results = OcfCompletion.CompletionResult(completion_args)
        self._as_parameter_ = self.callback
        self.context = context

    @property
    def callback(self):
        @CFUNCTYPE(c_void_p, *self.results.arg_types)
        def complete(*args):
            self.results.results = args
            self.e.set()

        return complete

    def wait(self):
        self.e.wait()


class OcfError(BaseException):
    def __init__(self, msg, error_code):
        super().__init__(self, msg)
        self.error_code = OcfErrorCode(abs(error_code))
        self.msg = msg

    def __str__(self):
        return "{} ({})".format(self.msg, repr(self.error_code))


class SharedOcfObject(Structure):
    _instances_ = {}

    def __init__(self):
        super().__init__()
        type(self)._instances_[self._as_parameter_] = self

    @classmethod
    def get_instance(cls, ref: int):
        try:
            return cls._instances_[ref]
        except:  # noqa E722
            logging.getLogger("pyocf").error(
                "OcfSharedObject corruption. wanted: {} instances: {}".format(
                    ref, cls._instances_
                )
            )
            return None

    @classmethod
    def del_object(cls, ref: int):
        del cls._instances_[ref]


class Uuid(Structure):
    _fields_ = [("_size", c_size_t), ("_data", c_char_p)]


class CacheLineSize(IntEnum):
    LINE_4KiB = S.from_KiB(4)
    LINE_8KiB = S.from_KiB(8)
    LINE_16KiB = S.from_KiB(16)
    LINE_32KiB = S.from_KiB(32)
    LINE_64KiB = S.from_KiB(64)
    DEFAULT = LINE_4KiB


class SeqCutOffPolicy(IntEnum):
    ALWAYS = 0
    FULL = 1
    NEVER = 2
    DEFAULT = FULL


class CacheLines(S):
    def __init__(self, count: int, line_size: CacheLineSize):
        self.bytes = count * line_size
        self.line_size = line_size

    def __int__(self):
        return int(self.bytes / self.line_size)
