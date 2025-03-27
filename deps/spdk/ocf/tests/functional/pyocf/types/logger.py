#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import (
    c_void_p,
    Structure,
    c_char_p,
    c_uint,
    c_int,
    cast,
    CFUNCTYPE,
    pointer,
)
from enum import IntEnum
import logging
from io import StringIO
import weakref

from ..ocf import OcfLib

logger = logging.getLogger("pyocf")
logger.setLevel(logging.DEBUG)


class LogLevel(IntEnum):
    EMERG = 0
    ALERT = 1
    CRIT = 2
    ERR = 3
    WARN = 4
    NOTICE = 5
    INFO = 6
    DEBUG = 7


LevelMapping = {
    LogLevel.EMERG: logging.CRITICAL,
    LogLevel.ALERT: logging.CRITICAL,
    LogLevel.CRIT: logging.CRITICAL,
    LogLevel.ERR: logging.ERROR,
    LogLevel.WARN: logging.WARNING,
    LogLevel.NOTICE: logging.INFO,
    LogLevel.INFO: logging.INFO,
    LogLevel.DEBUG: logging.DEBUG,
}


class LoggerOps(Structure):
    OPEN = CFUNCTYPE(c_int, c_void_p)
    CLOSE = CFUNCTYPE(None, c_void_p)
    # PRINTF ommited - we cannot make variadic function call in ctypes
    LOG = CFUNCTYPE(c_int, c_void_p, c_uint, c_char_p)
    PRINT_RL = CFUNCTYPE(c_int, c_void_p, c_char_p)
    DUMP_STACK = CFUNCTYPE(c_int, c_void_p)

    _fields_ = [
        ("_open", OPEN),
        ("_close", CLOSE),
        ("_print", c_void_p),
        ("_print_rl", PRINT_RL),
        ("_dump_stack", DUMP_STACK),
    ]


class LoggerPriv(Structure):
    _fields_ = [("_log", LoggerOps.LOG)]


class Logger(Structure):
    _instances_ = {}

    _fields_ = [("logger", c_void_p)]

    def __init__(self):
        self.ops = LoggerOps(
            _open=self._open,
            _print=cast(OcfLib.getInstance().pyocf_printf_helper, c_void_p),
            _close=self._close,
        )
        self.priv = LoggerPriv(_log=self._log)
        self._as_parameter_ = cast(pointer(self.priv), c_void_p).value
        self._instances_[self._as_parameter_] = weakref.ref(self)

    def get_ops(self):
        return self.ops

    def get_priv(self):
        return self.priv

    @classmethod
    def get_instance(cls, ctx: int):
        priv = OcfLib.getInstance().ocf_logger_get_priv(ctx)
        return cls._instances_[priv]()

    @staticmethod
    @LoggerOps.LOG
    def _log(ref, lvl, msg):
        Logger.get_instance(ref).log(lvl, str(msg, "ascii").strip())
        return 0

    @staticmethod
    @LoggerOps.OPEN
    def _open(ref):
        if hasattr(Logger.get_instance(ref), "open"):
            return Logger.get_instance(ref).open()
        else:
            return 0

    @staticmethod
    @LoggerOps.CLOSE
    def _close(ref):
        if hasattr(Logger.get_instance(ref), "close"):
            return Logger.get_instance(ref).close()
        else:
            return 0


class DefaultLogger(Logger):
    def __init__(self, level: LogLevel = LogLevel.WARN):
        super().__init__()
        self.level = level

        ch = logging.StreamHandler()
        fmt = logging.Formatter(
            "%(asctime)s - %(name)s - %(levelname)s - %(message)s"
        )
        ch.setFormatter(fmt)
        ch.setLevel(LevelMapping[level])
        logger.addHandler(ch)

    def log(self, lvl: int, msg: str):
        logger.log(LevelMapping[lvl], msg)

    def close(self):
        logger.handlers = []


class FileLogger(Logger):
    def __init__(self, f, console_level=None):
        super().__init__()
        fmt = logging.Formatter(
            "%(asctime)s - %(name)s - %(levelname)s - %(message)s"
        )

        fh = logging.FileHandler(f)
        fh.setLevel(logging.DEBUG)
        fh.setFormatter(fmt)

        logger.addHandler(fh)

        if console_level:
            sh = logging.StreamHandler()
            sh.setLevel(LevelMapping[console_level])
            sh.setFormatter(fmt)
            logger.addHandler(sh)

    def log(self, lvl, msg):
        logger.log(LevelMapping[lvl], msg)

    def close(self):
        logger.handlers = []


class BufferLogger(Logger):
    def __init__(self, level: LogLevel):
        super().__init__()
        self.level = level
        self.buffer = StringIO()

    def log(self, lvl, msg):
        if lvl < self.level:
            self.buffer.write(msg + "\n")

    def get_lines(self):
        return self.buffer.getvalue().split("\n")


lib = OcfLib.getInstance()
lib.ocf_logger_get_priv.restype = c_void_p
lib.ocf_logger_get_priv.argtypes = [c_void_p]
