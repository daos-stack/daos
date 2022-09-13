#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail

from dfuse_test_base import DfuseTestBase


class DfuseContainerCheck(DfuseTestBase):
    # pylint: disable=too-few-public-methods,too-many-ancestors
    """Base Dfuse Container check test class.

    :avocado: recursive
    """

    def test_dfuse_container_check(self):
        """Jira ID: DAOS-3635.

        Test Description:
            Purpose of this test is to try and mount different container types
            to dfuse and check the behavior.
        Use cases:
            Create pool
            Create container of type default
            Try to mount to dfuse and check the behavior.
            Create container of type POSIX.
            Try to mount to dfuse and check the behavior.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=dfusecontainercheck,test_dfuse_container_check
        """
        # get test params for cont and pool count
        cont_types = self.params.get("cont_types", '/run/container/*')

        # Create a pool and start dfuse.
        self.add_pool(connect=False)

        for cont_type in cont_types:
            # Get container params
            self.add_container(self.pool, create=False)
            # create container
            if cont_type == "POSIX":
                self.container.type.update(cont_type)
            self.container.create()

            # Attempt to mount the dfuse mount point - this should only succeed
            # with a POSIX container
            try:
                self.start_dfuse(
                    self.hostlist_clients, self.pool, self.container)
                if cont_type != "POSIX":
                    self.fail("Non-POSIX type container mounted over dfuse")

            except TestFail as error:
                if cont_type == "POSIX":
                    self.fail(
                        "POSIX type container failed dfuse mount: {}".format(
                            error))
                self.log.info(
                    "Non-POSIX type container expected to fail dfuse mount")

            # Verify dfuse is running on the POSIX type container
            if cont_type == "POSIX":
                self.dfuse.check_running()

            # Stop dfuse and destroy the container for next iteration
            if not cont_type == "":
                self.stop_dfuse()
            self.container.destroy(1)
