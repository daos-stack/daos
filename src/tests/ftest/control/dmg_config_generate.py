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

import os

from apricot import TestWithServers
from command_utils import CommandFailure


class ConfigGenerate(TestWithServers):
    """Test Class Description:

    Verify the veracity of the configuration created by the command and what
    the user specified, input verification and correct execution of the server
    once the generated configuration is propagated to the servers.

    This test assumes that systems have both nvme and dcpm available.

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

    def get_storage_info(self):
        """Get storage and network info expected from generated config file.

        This function uses dmg storage scan to extract the data we need to
        verify the generate config file.

        The interface value is obtained from the OFI_INTERFACE env variable,
        the number of NUMA nodes available is obtained from the sockets of pmem
        devices and nvme information is matched with pmem sockets.

        """
        host = self.hostlist_servers[0]
        data = self.get_dmg_command().storage_scan(verbose=True)
        pmem_info = data[host]["scm"]
        nvme_info = data[host]["nvme"]

        interface = os.environ.get("OFI_INTERFACE")
        pmem_sockets = set(pmem_info[p]["socket"] for p in pmem_info)

        # Get nvmes that match with pmem socket.
        # The dict would look like so: {'0': ['0000:5e:00.0', '0000:5f:00.0']}
        sockets_info = {socket: [] for socket in pmem_sockets}
        for nvme in nvme_info:
            if nvme_info[nvme]["socket"] in sockets_info:
                sockets_info[nvme_info[nvme]["socket"]].append(nvme)

        return {
            "pmem": len(sockets_info),
            "nvme": len(sockets_info.values()[0]),
            "net": interface
        }

    def verify_config(self, pmem, nvme, net):
        """Verify expected number of ioservers/nvme devices were started.

        Args:
            pmem (str): Number of SCM devices required per storage host
            nvme (str): Minimum number of NVMe devices required per storage host
            net (str): Network class preferred
        """
        # Get the generated values and the system storage info
        sys_info = self.get_storage_info()
        gen_info = self.server_managers[-1].manager.job.discovered_yaml
        if gen_info and "servers" in gen_info:
            g_pmem = len(gen_info["servers"])
            g_nvme = len([x["bdev_list"] for x in gen_info["servers"]][0])
            g_net = [x["fabric_iface"] for x in gen_info["servers"]]

            status = True
            if pmem is None and nvme is None:
                status = False if g_pmem != sys_info["pmem"] else True
                status = False if g_nvme != sys_info["nvme"] else True
            else:
                # If user provides pmem, we want to see the same value
                if pmem:
                    status = False if g_pmem != pmem else True
                # If user provides nvme, we want to check bounds
                if nvme:
                    if not g_nvme >= nvme and g_nvme != sys_info["nvme"]:
                        status = False

            # Check all ifaces are of same type and what user asked
            mapping = {"infiniband": "ib", "ethernet": "eth"}
            check = mapping[net] if net else sys_info["net"]
            for iface in g_net:
                if not iface.startswith(check[:2]):
                    status = False
                    break

            if not status:
                if pmem:
                    sys_info["pmem"] = pmem
                self.fail("Expected: {} Generated: {}".format(
                    sys_info, {"pmem": g_pmem, "nvme": g_nvme, "net": g_net}))
        else:
            self.fail("No discovered yaml info detected!")

    def run_test(self, pmem, nvme, net):
        """Run test.

        Args:
            pmem (str): Number of SCM devices required per storage host
            nvme (str): Minimum number of NVMe devices required per storage host
            net (str): Network class preferred (default: best-available)
        """
        self._start_servers = True
        self.server_managers[-1].manager.job.discover_pmem.value = pmem
        self.server_managers[-1].manager.job.discover_nvme.value = nvme
        self.server_managers[-1].manager.job.discover_net.value = net

        # Start up the servers in discovery mode and generate config with dmg
        self.start_server_managers()

        # Verify generated config file
        self.verify_config(pmem, nvme, net)

    def test_dmg_config_generate_1(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        On verification and the args are all None, we should see the default
        values in the yaml file.
        - For pmems, we should see the number of detected NUMA nodes on the
        host (2).
        - For nvme, we should see the number of SSDs that are bound to the
        NUMA node matching the pmem device (2).
        - For network class, the most performant should be selected (ib).

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_1
        """
        self.run_test(None, None, None)

    def test_dmg_config_generate_2(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        This test is specifying minimum nvme value, we should see per available
        server: len(SSDs) >= 1

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_2
        """
        self.run_test(None, 1, None)

    def test_dmg_config_generate_3(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        This test is specifying minimum nvme value, we should see per available
        server: len(SSDs) >= 2

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_3
        """
        self.run_test(None, 2, None)

    def test_dmg_config_generate_4(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        This test is defining the amount of io servers that will be configured.
        In this test case, we expect to see 1 io server configuration per host.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_4
        """
        self.run_test(1, None, None)

    def test_dmg_config_generate_5(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        This test is defining the amount of io servers that will be configured.
        In this test case, we expect to see 2 io server configurations per host.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_4
        """
        self.run_test(2, None, None)

    def test_dmg_config_generate_6(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        This test is defining the amount of io servers that will be configured
        and min nvme devices.
        In this test case, we expect to see 1 io server configuration per host
        and len(SSDs) >= 1 for each of the io server instances.In addition,
        each io server instance should be configured to use ethernet.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_6
        """
        self.run_test(1, 1, "ethernet")

    def test_dmg_config_generate_7(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        This test is defining the amount of io servers that will be configured
        and min nvme devices.
        In this test case, we expect to see 2 io server configuration per host
        and len(SSDs) >= 2 for each of the io server instances. In addition,
        each io server instance should be configured to use infiniband.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_7
        """
        self.run_test(2, 2, "infiniband")

    def test_dmg_config_generate_8(self):
        """
        JIRA ID: DAOS-5986

        Test Description:
        Verify that dmg can generate an accurate configuration file.

        This test is defining the amount of io servers that will be configured
        and min nvme devices.
        In this test case, we expect to see 10 io server configuration per host
        and len(SSDs) >= 10 for each of the io server instances.
        We expect this test to fail since requested storage is not available.

        :avocado: tags=all,small,hw,full_regression,config_generate
        :avocado: tags=config_generate_8
        """
        try:
            self.run_test(10, 10, "infiniband")
        except CommandFailure as error:
            self.log.info("Expected failure: %s", error)
