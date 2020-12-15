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

from apricot import TestWithServers
from command_utils import CommandFailure


class ConfigGenerate(TestWithServers):
    """Test Class Description:

    Verify the veracity of the configuration created by the command and what
    the user specified, input verification and correct execution of the server
    once the generated configuration is propagated to the servers.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ConfigGenerate object."""
        super(ConfigGenerate, self).__init__(*args, **kwargs)
        self._start_servers = False

    def start_server_managers(self):
        """Start the daos_server processes on each specified list of hosts."""
        if self._start_servers:
            super(ConfigGenerate, self).start_server_managers()

    def run_test(self, pmem, nvme, net):
        """Run test."""
        self._start_servers = True
        self.server_managers[-1].manager.job.discover_pmem.value = pmem
        self.server_managers[-1].manager.job.discover_nvme.value = nvme
        self.server_managers[-1].manager.job.discover_net.value = net

        self.start_server_managers()

    def test_dmg_config_generate_1(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_1
        """
        self.run_test(None, None, None)

    def test_dmg_config_generate_2(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_2
        """
        self.run_test(None, 1, None)

    def test_dmg_config_generate_3(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_3
        """
        self.run_test(None, 2, None)

    def test_dmg_config_generate_4(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_4
        """
        self.run_test(1, None, None)

    def test_dmg_config_generate_5(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_4
        """
        self.run_test(2, None, None)

    def test_dmg_config_generate_6(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_6
        """
        self.run_test(1, 1, "ethernet")

    def test_dmg_config_generate_7(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_7
        """
        self.run_test(2, 2, "infiniband")

    def test_dmg_config_generate_8(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_8
        """
        try:
            self.run_test(10, 10, "infiniband")
        except CommandFailure as error:
            self.log.info("Expected failure: {}".format(error))
