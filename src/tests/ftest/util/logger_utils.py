"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import logging
import sys

DATE_FORMAT = r"%Y/%m/%d %I:%M:%S"
LOG_FILE_FORMAT = "%(asctime)s %(levelname)-5s %(funcName)30s: %(message)s"


def get_console_handler(log_format, log_level):
    """Get a logging console (stream) handler.

    Args:
        log_format (str): the logging format
        log_level (int): the logging level

    Returns:
        logging.StreamHandler: a logging handler for console messages

    """
    console_handler = logging.StreamHandler()
    console_handler.setLevel(log_level)
    console_handler.setFormatter(logging.Formatter(log_format, datefmt=DATE_FORMAT))
    return console_handler


def get_file_handler(log_file, log_format, log_level):
    """Get a logging file handler.

    Args:
        log_file (str): the file which will contain the log messages
        log_format (str): the logging format
        log_level (int): the logging level

    Returns:
        logging.FileHandler: a logging handler for messages included in the file

    """
    log_handler = logging.FileHandler(log_file, encoding='utf-8')
    log_handler.setLevel(log_level)
    log_handler.setFormatter(logging.Formatter(log_format, datefmt=DATE_FORMAT))
    return log_handler


class TestLogger():
    """Defines a Logger that also logs messages to DaosLog."""

    _LEVELS = {
        "debug": logging.DEBUG,
        "info": logging.INFO,
        "warning": logging.WARNING,
        "error": logging.ERROR
    }

    def __init__(self, logger, daos_logger):
        """Initialize the TestLogger object.

        Args:
            logger (Logger): the test logger
            daos_logger (DaosLog): daos logging object
        """
        self.log = logger
        self.daos_log = daos_logger

    def _log(self, method, msg, *args, **kwargs):
        """Log 'msg % args' with the specified logging method.

        Args:
            msg (str): message to log
        """
        getattr(self.log, method)(msg, *args, **kwargs)
        if self.daos_log is not None:
            # Convert the optional args and kwargs into a message string
            if "exc_info" in kwargs:
                if isinstance(kwargs["exc_info"], BaseException):
                    kwargs["exc_info"] = (
                        type(kwargs["exc_info"]), kwargs["exc_info"],
                        kwargs["exc_info"].__traceback__
                    )
                elif not isinstance(kwargs["exc_info"], tuple):
                    kwargs["exc_info"] = sys.exc_info()
            else:
                kwargs["exc_info"] = None
            record = self.log.makeRecord(
                self.log.name, self._LEVELS[method], "(unknown file)", 0, msg,
                args, **kwargs)
            getattr(self.daos_log, method)(record.getMessage())

    def debug(self, msg, *args, **kwargs):
        """Log 'msg % args' with severity 'DEBUG'.

        Args:
            msg (str): message to log
        """
        self._log("debug", msg, *args, **kwargs)

    def info(self, msg, *args, **kwargs):
        """Log 'msg % args' with severity 'INFO'.

        Args:
            msg (str): message to log
        """
        self._log("info", msg, *args, **kwargs)

    def warning(self, msg, *args, **kwargs):
        """Log 'msg % args' with severity 'WARNING'.

        Args:
            msg (str): message to log
        """
        self._log("warning", msg, *args, **kwargs)

    def error(self, msg, *args, **kwargs):
        """Log 'msg % args' with severity 'ERROR'.

        Args:
            msg (str): message to log
        """
        self._log("error", msg, *args, **kwargs)
