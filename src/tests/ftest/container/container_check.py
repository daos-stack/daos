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
        :avocado: tags=all,small,full_regression,dfusecontainercheck
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
