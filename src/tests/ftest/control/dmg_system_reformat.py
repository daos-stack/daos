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
from __future__ import print_function

from avocado import fail_on
from apricot import TestWithServers
from server_utils import ServerFailed
from command_utils import CommandFailure


class DmgSystemReformatTest(TestWithServers):
    # pylint: disable=too-many-ancestors
    """Test Class Description:

    Test to verify dmg storage format reformat option works as expected on a
    DAOS system after a controlled shutdown.

    :avocado: recursive
    """

    @fail_on(CommandFailure)
    def create_pool_at_full_capacity(self):
        """Create a pool at full capacity.

        Raises:
            CommandFailure: raise exception if pool creation fails.

        """
        host = self.hostlist_servers[0]

        storage_info = self.get_dmg_command().storage_scan()
        scm_capacity = storage_info[host]["scm"]["capacity"]
        nvme_capacity = storage_info[host]["nvme"]["capacity"]

        # Create pool object
        self.add_pool(create=False, connect=False)

        # update pool size to full capacity
        self.pool.scm_size.update(scm_capacity)
        self.pool.nvme_size.update(nvme_capacity)
        self.pool.create()

    @fail_on(CommandFailure)
    @fail_on(ServerFailed)
    def test_dmg_system_reformat(self):
        """
        JIRA ID: DAOS-5415

        Test Description: Test dmg system reformat functionality.

        :avocado: tags=all,small,pr,hw,control,sys_reformat,dmg
        """
        self.create_pool_at_full_capacity()

        self.log.info("Disable raising an exception to check for ENOSPACE \
                       error on pool create")
        self.get_dmg_command().exit_status_exception = False

        # Try to create second pool and check that it fails with ENOSPACE
        self.get_pool()
        if self.get_dmg_command().result.exit_status != 0:
            self.log.info("Pool create failed: %s",
                          self.get_dmg_command().result.stderr)
            if "ENOSPACE" not in self.get_dmg_command().result.stderr:
                self.fail("Pool create did not fail do to ENOSPACE!")

        self.log.info("Re-enable raising exceptions for dmg.")
        self.get_dmg_command().exit_status_exception = True

        self.log.info("Stop running io_server instancess: 'dmg system stop'")
        self.get_dmg_command().system_stop(force=True)
        if self.get_dmg_command().result.exit_status != 0:
            self.fail("Detected issues performing a system stop: {}".format(
                self.get_dmg_command().result.stderr))

        self.log.info("Perform dmg storage format on all system ranks:")
        self.get_dmg_command().storage_format(reformat=True)
        if self.get_dmg_command().result.exit_status != 0:
            self.fail("Detected issues performing storage format: {}".format(
                self.get_dmg_command().result.stderr))

        # Check that io_servers starts up again
        self.log.info("<SERVER> Waiting for the daos_io_servers to start")
        self.server_managers[-1].manager.job.pattern_count = 2
        if not self.server_managers[-1].manager.job.\
           check_subprocess_status(self.server_managers[-1].manager.process):
            self.server_managers[-1].kill()
            raise ServerFailed("Failed to start servers after format")

        # Check that we have cleared storage by checking pool list
        if self.get_dmg_command().pool_list():
            self.fail("Detected pools in storage after refomat: {}".format(
                self.get_dmg_command().result.stdout))

        # Remove pools and containers since they were wiped from memory
        self.pool = None
        self.container = None
        self.log.info("Create pool at full capacity again to verify storage \
                       has cleared.")
        self.create_pool_at_full_capacity()
