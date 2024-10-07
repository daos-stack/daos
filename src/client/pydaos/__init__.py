# (C) Copyright 2019-2024 Intel Corporation.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# pylint: disable=consider-using-f-string,cyclic-import
"""
PyDAOS Module allowing global access to the DAOS containers and objects.
"""

import atexit

from . import pydaos_shim  # pylint: disable=relative-beyond-top-level,import-self

DAOS_MAGIC = 0x7A8A


class DaosErrorCode():
    """Class to represent a daos error code.

    Class for translating from numerical values to names/messages.
    err_num should be negative as passed to this function.

    This is not an exception class.
    """

    # pylint: disable=too-few-public-methods
    def __init__(self, err_num):
        self.err = err_num

        try:
            (self.name, self.message) = pydaos_shim._errors[-err_num]
        except KeyError:
            self.name = "DER_UNKNOWN"
            self.message = f"Unknown error code {err_num}"

    def __str__(self):
        return f'DAOS error code {self.err} ({self.name}): "{self.message}"'


# Define the PyDError class here before doing the pydaos_core import so that
# it's accessible from within the module.
class PyDError(Exception):
    """PyDAOS exception when operation cannot be completed."""

    def __init__(self, message, rc):  # pylint: disable=super-init-not-called
        self.error = DaosErrorCode(rc)
        self.message = f'{message}: {str(self.error)}'

    def __str__(self):
        return self.message


class DaosClient():
    # pylint: disable=too-few-public-methods
    # pylint: disable=attribute-defined-outside-init
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


from .pydaos_core import *  # noqa: F403,E402

__all__ = ["pydaos_core"]  # noqa: F405
