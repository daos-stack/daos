# (C) Copyright 2019-2021 Intel Corporation.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
"""
PyDAOS Module allowing global access to the DAOS containers and objects.
"""

import sys
import atexit

DAOS_MAGIC = 0x7A89

# pylint: disable=import-error
if sys.version_info < (3, 0):
    from . import pydaos_shim_27 as pydaos_shim
else:
    from . import pydaos_shim_3 as pydaos_shim
# pylint: enable=import-error

# Define the PyDError class here before doing the pydaos_core import so that
# it's accessible from within the module.
class PyDError(Exception):
    """PyDAOS exception when operation cannot be completed."""

    # DER_* error code is printed in both integer and string format where
    # possible.  There is an odd effect with daos_init() errors that
    # pydaos_shim is valid during __init__ but None during __str__ so format
    # the string early and just report it later on.
    def __init__(self, message, rc):
        err = pydaos_shim.err_to_str(DAOS_MAGIC, rc)
        if err:
            self.message = '{}: {}'.format(message, err)
        else:
            self.message = '{}: {}'.format(message, rc)

    def __str__(self):
        return self.message

class DaosClient():
    # pylint: disable=too-few-public-methods
    """
    DaosClient is responsible for handling DAOS init/fini.

    The class implements the Singleton pattern and only
    allows a single instance to be instantiated during
    the lifetime of a process.
    """
    _instance = None

    @classmethod
    def cleanup(cls):
        """Trigger the instance cleanup process."""
        if cls._instance is None:
            return
        cls._instance = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            # pylint: disable=protected-access
            cls._instance._open()
        return cls._instance

    def _open(self):
        # Initialize DAOS
        self.connected = False
        _rc = pydaos_shim.daos_init(DAOS_MAGIC)
        if _rc != pydaos_shim.DER_SUCCESS:
            raise PyDError("Failed to initialize DAOS", _rc)
        self.connected = True

    def _close(self):
        if not self.connected:
            return
        _rc = pydaos_shim.daos_fini(DAOS_MAGIC)
        if _rc != pydaos_shim.DER_SUCCESS:
            raise PyDError("Failed to cleanup DAOS", _rc)
        self.connected = False

    def __del__(self):
        if not pydaos_shim or not self.connected:
            return
        self._close()

@atexit.register
def _cleanup():
    DaosClient.cleanup()

from .pydaos_core import *

__all__ = ["pydaos_core"]
