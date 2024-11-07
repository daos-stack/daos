# (C) Copyright 2019-2024 Intel Corporation.
# (C) Copyright 2024 Google LLC
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
"""
PyTorch DAOS Module allowing using DFS as Dataset
"""

import atexit

from . import torch_shim  # pylint: disable=relative-beyond-top-level,import-self

DAOS_MAGIC = 0x7A8B


class PyDError(Exception):
    """PyDAOS exception when operation cannot be completed."""

    # DER_* error code is printed in both integer and string format where
    # possible.  There is an odd effect with daos_init() errors that
    # torch_shim is valid during __init__ but None during __str__ so format
    # the string early and just report it later on.
    def __init__(self, message, rc):  # pylint: disable=super-init-not-called
        err = torch_shim.err_to_str(DAOS_MAGIC, rc)
        if err:
            self.message = f"{message}: {err} (rc = {rc})"
        else:
            self.message = f"{message}: {rc}"

    def __str__(self):
        return self.message


# The module loader procedure guarantees that __init__.py is going to be run only once
_rc = torch_shim.module_init()
if _rc != 0:
    raise ValueError(f"Could not initialise DAOS module: rc={_rc}")


@atexit.register
def _fini():
    rc = torch_shim.module_fini()
    if rc != 0:
        # torch_shim module is no longer usable at this point so as err_to_str call
        raise ValueError(f"Could not finalise DAOS module, rc={rc}")


from .torch_api import *  # noqa: F403,E402


__all__ = ["torch_api"]  # noqa: F405
