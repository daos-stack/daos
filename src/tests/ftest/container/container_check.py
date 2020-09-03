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
from dfuse_test_base import DfuseTestBase
from command_utils import CommandFailure


class DfuseContainerCheck(DfuseTestBase):
    # pylint: disable=too-few-public-methods
    """Base Dfuse Container check test class.

    :avocado: recursive
    """

    def test_dfusecontainercheck(self):
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
            self.add_container(create=False)
            # create container
            if cont_type == "POSIX":
                self.container.type.update(cont_type)
            self.container.create()
            try:
                # mount fuse
                self.start_dfuse(
                    self.hostlist_clients, self.pool, self.container)
                # check if fuse got mounted
                self.dfuse.check_running()
                # fail the test if fuse mounts with non-posix type container
                if cont_type == "":
                    self.fail("Non-Posix type container got mounted over dfuse")
            except CommandFailure as error:
                # expected to throw CommandFailure exception for non-posix type
                # container
                if cont_type == "":
                    self.log.info("Expected behavior: Default container type \
                        is expected to fail on dfuse mount: %s", str(error))
                # fail the test if exception is caught for POSIX type container
                elif cont_type == "POSIX":
                    self.log.error("Posix Container dfuse mount \
                        failed: %s", str(error))
                    self.fail("Posix container type was expected to mount \
                        over dfuse")
            # stop fuse and container for next iteration
            if not cont_type == "":
                self.stop_dfuse()
            self.container.destroy(1)
