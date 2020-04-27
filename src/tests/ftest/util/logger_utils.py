#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
from logging import DEBUG, INFO, WARNING, ERROR
import sys


class TestLogger(object):
    """Defines a Logger that also logs messages to DaosLog."""

    _LEVELS = {
        "debug": DEBUG,
        "info": INFO,
        "warning": WARNING,
        "error": ERROR
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
