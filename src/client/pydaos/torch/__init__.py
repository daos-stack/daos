# (C) Copyright 2024 Intel Corporation.
# (C) Copyright 2024 Google LLC
# (C) Copyright 2024 Enakta Labs Ltd
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


# The module loader procedure guarantees that __init__.py is going to be run only once
_rc = torch_shim.module_init()
if _rc != 0:
    raise ValueError(f"Could not initialize DAOS module: rc={_rc}")


@atexit.register
def _fini():
    rc = torch_shim.module_fini()
    if rc != 0:
        raise ValueError(f"Could not finalize DAOS module, rc={rc}")


from .torch_api import *  # noqa: F403,E402

__all__ = ["torch_api"]  # noqa: F405
