#!/usr/bin/python
"""
(C) Copyright 2018-2020 Intel Corporation.

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

from ior_test_base import IorTestBase


class IorHdf5(IorTestBase):
    # pylint: disable=too-few-public-methods
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR/HDF5 on 2 server with basic parameters.

    :avocado: recursive
    """

    def test_ior_hdf5(self):
        """Jira ID: DAOS-3657.

        Test Description:
            Purpose of this test is to have small ior test to check basic
            functionality for HDF5 api

        Use case:
            Run IOR on HDF5 using a single shared file
            Generate 2 terabytes of data with IOR and read back and verify

        :avocado: tags=all,full_regression,hw,large,daosio,hdf5,iorhdf5
        """
        self.run_ior_with_pool()

    def test_ior_hdf5_vol(self):
        """Jira ID: DAOS-4909.

        Test Description:
            Purpose of this test is to have ior test to check basic
            functionality for HDF5 api using the vol connector

        Use case:
            Run IOR on HDF5 with vol connector using a single shared file
            Generate 2 terabytes of data with IOR and read back and verify

        :avocado: tags=all,full_regression,large,daosio,hdf5,vol,iorhdf5vol
        """
        hdf5_plugin_path = self.params.get("plugin_path", '/run/hdf5_vol/*')
        self.run_ior_with_pool(plugin_path=hdf5_plugin_path)
