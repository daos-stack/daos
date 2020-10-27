#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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
import write_host_file

from avocado import fail_on
from avocado.utils import process
from apricot import TestWithServers
from env_modules import load_mpi
from general_utils import get_log_file
from command_utils import CommandFailure
from agent_utils import include_local_host


class DaosCoreBase(TestWithServers):
    """Runs the daos_test subtests with multiple servers.

    :avocado: recursive
    """

    TEST_PATH = "/run/daos_tests/Tests/*"

    def __init__(self, *args, **kwargs):
        """Initialize the DaosCoreBase object."""
        super(DaosCoreBase, self).__init__(*args, **kwargs)
        self.subtest_name = None

        test_timeout = self.params.get("test_timeout", self.TEST_PATH)
        if test_timeout:
            self.timeout = test_timeout

    def setUp(self):
        """Set up before each test."""
        self.subtest_name = self.params.get("test_name", self.TEST_PATH)
        self.subtest_name = self.subtest_name.replace(" ", "_")

        # obtain separate logs
        self.update_log_file_names(self.subtest_name)

        super(DaosCoreBase, self).setUp()

        # if no client specified update self.hostlist_clients to local host
        # and create a new self.hostfile_clients.
        if self.hostlist_clients is None:
            self.hostlist_clients = include_local_host(self.hostlist_clients)
            self.hostfile_clients = write_host_file.write_host_file(
                self.hostlist_clients, self.workdir, None)

    @fail_on(CommandFailure)
    def start_server_managers(self):
        """Start the daos_server processes on each specified list of hosts.

        Enable scalable endpoint if requested with a test-specific
        'scalable_endpoint' yaml parameter.
        """
        # Enable scalable endpoint (if requested) prior to starting the servers
        scalable_endpoint = self.params.get("scalable_endpoint", self.TEST_PATH)
        if scalable_endpoint:
            for server_mgr in self.server_managers:
                for server_params in server_mgr.manager.job.yaml.server_params:
                    # Number of CaRT contexts should equal or be greater than
                    # the number of DAOS targets
                    targets = server_params.get_value("targets")

                    # Convert the list of variable assignments into a dictionary
                    # of variable names and their values
                    env_vars = server_params.get_value("env_vars")
                    env_dict = {
                        item.split("=")[0]: item.split("=")[1]
                        for item in env_vars}
                    env_dict["CRT_CTX_SHARE_ADDR"] = "1"
                    if "CRT_CTX_NUM" not in env_dict or \
                            int(env_dict["CRT_CTX_NUM"]) < int(targets):
                        env_dict["CRT_CTX_NUM"] = str(targets)
                    server_params.set_value("crt_ctx_share_addr", 1)
                    server_params.set_value(
                        "env_vars",
                        ["=".join(items) for items in env_dict.items()]
                    )

        # Start the servers
        super(DaosCoreBase, self).start_server_managers()

    def run_subtest(self):
        """Run daos_test with a subtest argument."""
        subtest = self.params.get("daos_test", self.TEST_PATH)
        num_clients = self.params.get("num_clients",
                                      '/run/daos_tests/num_clients/*')
        scm_size = self.params.get("scm_size", '/run/pool/*')
        nvme_size = self.params.get("nvme_size", '/run/pool/*')
        args = self.params.get("args", self.TEST_PATH, "")
        dmg = self.get_dmg_command()
        dmg_config_file = dmg.yaml.filename
        self.client_mca += " --mca btl_tcp_if_include eth0"

        cmd = " ".join(
            [
                self.orterun,
                self.client_mca,
                "-n", str(num_clients),
                "--hostfile", self.hostfile_clients,
                "-x", "=".join(["D_LOG_FILE", get_log_file(self.client_log)]),
                "--map-by node", "-x", "D_LOG_MASK=DEBUG",
                "-x", "DD_MASK=mgmt,io,md,epc,rebuild",
                self.daos_test,
                "-n", dmg_config_file,
                "".join(["-", subtest]),
                str(args)
            ]
        )

        env = {}
        env['CMOCKA_XML_FILE'] = os.path.join(self.outputdir, "%g_results.xml")
        env['CMOCKA_MESSAGE_OUTPUT'] = "xml"
        env['POOL_SCM_SIZE'] = "{}".format(scm_size)
        if not nvme_size:
            nvme_size = 0
        env['POOL_NVME_SIZE'] = "{}".format(nvme_size)

        load_mpi("openmpi")
        try:
            process.run(cmd, env=env)
        except process.CmdError as result:
            if result.result.exit_status != 0:
                # fake a JUnit failure output
                self.create_results_xml(self.subtest_name, result)
                self.fail(
                    "{0} failed with return code={1}.\n".format(
                        cmd, result.result.exit_status))

    def create_results_xml(self, testname, result):
        """Create a JUnit result.xml file for the failed command.

        Args:
            testname (str): name of the test
            result (CmdResult): result of the failed command.
        """
        filename = "".join([testname, "_results.xml"])
        filename = os.path.join(self.outputdir, filename)
        try:
            with open(filename, "w") as results_xml:
                results_xml.write('''<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="{0}" errors="1" failures="0" skipped="0" tests="1" time="0.0">
  <testcase name="ALL" time="0.0" >
    <error message="Test failed to start up"/>
    <system-out>
<![CDATA[{1}]]>
    </system-out>
    <system-err>
<![CDATA[{2}]]>
    </system-err>
  </testcase>
</testsuite>'''.format(testname, result.result.stdout, result.result.stderr))
        except IOError as error:
            self.log.error("Error creating %s: %s", filename, error)
