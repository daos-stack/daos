# (C) Copyright 2024 Intel Corporation.
# (C) Copyright 2024-2025 Google LLC
# (C) Copyright 2024-2025 Enakta Labs Ltd
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# pylint: disable=cyclic-import
"""
PyTorch DAOS Module allowing using DFS as Dataset
"""
import atexit

from . import torch_shim  # pylint: disable=relative-beyond-top-level,import-self

DAOS_MAGIC = 0x7A8B


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
        _rc = torch_shim.module_init(DAOS_MAGIC)
        if _rc != 0:
            raise ValueError(f"Could not initialize DAOS module: rc={_rc}")
        self.connected = True

    def _close(self):
        if not self.connected:
            return
        _rc = torch_shim.module_fini(DAOS_MAGIC)
        if _rc != 0:
            raise ValueError(f"Could not finalize DAOS module: rc={_rc}")
        self.connected = False

    def __del__(self):
        if not torch_shim or not self.connected:
            return
        self._close()


@atexit.register
def _cleanup():
    DaosClient.cleanup()


from .torch_api import *  # noqa: F403,E402

__all__ = ["torch_api"]  # noqa: F405
