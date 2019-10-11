# (C) Copyright  2019 Intel Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
# The Government's rights to use, modify, reproduce, release, perform, display,
# or disclose this software are subject to the terms of the Apache License as
# provided in Contract No. B609815.
# Any reproduction of computer software, computer software documentation, or
# portions thereof marked with this legend must also reproduce the markings.
"""
PyDAOS Module allowing global access to the DAOS containers and objects.
"""

import sys
import atexit

DAOS_MAGIC = 0x7A89

# pylint: disable=no-member
# pylint: disable=exec-used
# pylint: disable=import-error
if sys.version_info < (3, 0):
    import _pydaos_shim_27 as pydaos_shim
else:
    import ._pydaos_shim_3 as pydaos_shim
# pylint: enable=import-error

from .pydaos_core import *

__all__ = ["pydaos_core"]

class PyDError(Exception):
    """
    PyDAOS exception when operation cannot be completed.
    DER_* error code is printed in both integer and string format.
    """
    def __init__(self, message, rc):
        self.message = message
        self.rc = rc

    def __str__(self):
        err = pydaos_shim.err_to_str(DAOS_MAGIC, self.rc)
        if err is not None:
            return self.message + ": " + err
        else:
            return self.message

# Initialize DAOS
_rc = pydaos_shim.daos_init(DAOS_MAGIC)
if _rc != pydaos_shim.DER_SUCCESS:
    raise PyDError("Failed to initialize DAOS", _rc)

@atexit.register
def _cleanup():
    rc = pydaos_shim.daos_fini(DAOS_MAGIC)
    raise PyDError("Failed to cleanup DAOS", rc)
