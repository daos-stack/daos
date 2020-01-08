#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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


class TestLogger(object):
    """Defines a Logger that also logs messages to DaosLog."""

    def __init__(self, logger, daos_logger):
        """Initialize the logger with a name and an optional level.

        Args:
            daos_logger (DaosLog): [description]
        """
        self.log = logger
        self.daos_log = daos_logger

    def debug(self, msg, *args, **kwargs):
        """Log 'msg % args' with severity 'DEBUG'.

        Args:
            msg (str): message to log
        """
        self.log.debug(msg, *args, **kwargs)
        if self.daos_log is not None:
            self.daos_log.debug(msg)

    def info(self, msg, *args, **kwargs):
        """Log 'msg % args' with severity 'INFO'.

        Args:
            msg (str): message to log
        """
        self.log.info(msg, *args, **kwargs)
        if self.daos_log is not None:
            self.daos_log.info(msg)

    def warning(self, msg, *args, **kwargs):
        """Log 'msg % args' with severity 'WARNING'.

        Args:
            msg (str): message to log
        """
        self.log.warning(msg, *args, **kwargs)
        if self.daos_log is not None:
            self.daos_log.warning(msg)

    def error(self, msg, *args, **kwargs):
        """Log 'msg % args' with severity 'ERROR'.

        Args:
            msg (str): message to log
        """
        self.log.error(msg, *args, **kwargs)
        if self.daos_log is not None:
            self.daos_log.error(msg)
