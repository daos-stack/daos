#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import avocado
from apricot import TestWithServers
from pydaos.raw import DaosApiError
from command_utils import CommandFailure


class SCMConfigTest(TestWithServers):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Simple test to verify the SCM storage config.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a SCMConfigTest object."""
        super(SCMConfigTest, self).__init__(*args, **kwargs)
        self.obj = None

    @avocado.fail_on(DaosApiError)
    def write_data(self, data):
        """Write data obj to a container.

        Args:
            data (str): string of data to be written.
        """
        # create an object and write some data into it
        self.obj = self.container.container.write_an_obj(data,
                                                         len(data) + 1,
                                                         "dkey",
                                                         "akey",
                                                         obj_cls="OC_S1")
        self.obj.close()
        self.log.info("==>    Wrote an object to the container")

    def test_scm_in_use_basic(self):
        """
        JIRA ID: DAOS-2972

        Test Description: Verify that an attempt to configure devices that have
        already been configured and are in use by DAOS is handled.

        :avocado: tags=all,small,pr,daily_regression,hw,scm_in_use,basic
        """
        # Create pool and container
        self.prepare_pool()
        self.add_container(self.pool)

        # now open the container and write some data
        self.container.open()
        data_w = "Ehrm... Testing... Testing"
        self.write_data(data_w)

        # Run storage prepare
        if self.server_managers[-1].manager.job.using_dcpm:
            self.log.info("==>    Verifying storage prepare is done")
            kwargs = {"scm": True, "force": True}
            try:
                self.server_managers[-1].dmg.storage_prepare(**kwargs)
            except CommandFailure as error:
                self.fail("Storage prepare failure: {}".format(error))
        else:
            self.fail("Detected dcpm not specified")

        # Check that after storage prepare we still have container data
        try:
            self.obj.open()
            data_r = self.container.container.read_an_obj(
                len(data_w) + 1, "dkey", "akey", self.obj)
        except DaosApiError as error:
            self.fail(
                "Error retrieving the container data:\n{0}".format(error))

        # Compare the written data and read data
        msg = "Written and read data not equal"
        self.assertEqual(data_w, data_r.value, msg)

        # Lets make sure we can still write data after preparing.
        data_w2 = "Almost done testing... just this last thing."
        self.write_data(data_w2)
