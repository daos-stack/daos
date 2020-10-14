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
from command_utils_base import BasicParameter
from command_utils import ExecutableCommand


class Hdf5VolCommand(ExecutableCommand):
    # pylint: disable=too-few-public-methods
    """Defines a object for executing HDF5 VOL commands."""

    def __init__(self, path):
        """Create an Hdf5VolCommand object.

        Args:
            path (str): path for the testname parameter
        """
        super(Hdf5VolCommand, self).__init__("/run/hdf5_vol/*", None, path)

        self.testname = BasicParameter(default="hostname")
