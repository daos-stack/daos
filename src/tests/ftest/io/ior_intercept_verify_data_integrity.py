#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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
import threading
import write_host_file
from ior_test_base import IorTestBase
from ior_utils import IorCommand
from command_utils import CommandFailure


class IorInterceptDfuseMix(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR with mix of dfuse and 
       interception library on a single server and multi client
       settings with basic parameters.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super(IorInterceptDfuseMix, self).setUp()
        # Following line can be removed once the constraint in the 
        # IorTestBase is resolved. #DAOS-3320
        self.hostlist_clients = self.params.get(
            "test_clients", "/run/hosts/*")
        self.lock = threading.Lock()

    def test_ior_intercept_dfuse_mix(self):
        """Jira ID: DAOS-3502.

        Test Description:
            Purpose of this test is to run ior through dfuse with
            interception library  on 5 clients and without interception
            library on 1 client for at least 30 minutes and verify the
            data integrity using ior's Read Verify and Write Verify
            options.

        Use case:
            Run ior with read, write, fpp, read verify
            write verify for 30 minutes
            Run ior with read, write, read verify
            write verify for 30 minutes

        :avocado: tags=all,daosio,hw,full_regression,iorinterceptverifydata
        """
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        with_intercept = dict()
        self.run_multiple_ior_with_pool(with_intercept, intercept)
        self.log_metrics(with_intercept)

        IorCommand.log_metrics(self.log, "5 clients - with " +
                               "interception library", with_intercept[1])
        IorCommand.log_metrics(self.log, "1 client - without " +
                               "interception library", with_intercept[2])

    def run_multiple_ior_with_pool(self, results, intercept=None):
        """Execute ior with optional overrides for ior flags and object_class.

        If specified the ior flags and ior daos object class parameters will
        override the values read from the yaml file.

        Args:
            intercept (str): path to the interception library. Shall be used
                             only for POSIX through DFUSE.
            ior_flags (str, optional): ior flags. Defaults to None.
            object_class (str, optional): daos object class. Defaults to None.
        """
        # Create a pool if one does not already exist
        if self.pool is None:
            self.create_pool()
        # Update IOR params with the pool
        self.ior_cmd.set_daos_params(self.server_group, self.pool)

        # start dfuse if api is POSIX
        if self.ior_cmd.api.value == "POSIX":
            # Connect to the pool, create container and then start dfuse
            # Uncomment below two lines once DAOS-3355 is resolved
            # self.pool.connect()
            # self.create_cont()
            self.start_dfuse()

        # Create two jobs and run in parallel.
        # Job1 will have 3 client set up to use dfuse + interception
        # library
        # Job2 will have 1 client set up to use only dfuse.
        job1 = self.get_new_job(self.hostlist_clients[:-1], 1,
                                results, intercept)
        job2 = self.get_new_job([self.hostlist_clients[-1]], 2,
                                results, None)

        job1.start()
        job2.start()
        job1.join()
        job2.join()

    def get_new_job(self, clients, job_num, results, intercept=None):
        """Create a new thread for ior run

        Args:
            clients (lst): Number of clients the ior would run against.
            job_num (int): Assigned job number
            results (dict): A dictionary object to store the ior metrics
            intercept (path): Path to interception library
        """
        hostfile = write_host_file.write_host_file(
            clients, self.workdir, self.hostfile_clients_slots)
        job = threading.Thread(target=self.run_multiple_ior, args=(
            hostfile, len(clients), results, job_num, intercept))
        return job

    def run_multiple_ior(self, hostfile, num_clients,
                         results, job_num, intercept=None):
        #pylint: disable=too-many-arguments
        """Run the IOR command.

        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
            intercept (str): path to interception library.
        """
        self.lock.acquire(True)
        self.ior_cmd.test_file.update(self.dfuse.mount_dir.value
                                      + "/testfile{}".format(job_num))
        manager = self.get_job_manager_command()
        procs = (self.processes // len(self.hostlist_clients)) * num_clients
        env = self.ior_cmd.get_default_env(
            str(manager), self.tmp, self.client_log)
        if intercept:
            env["LD_PRELOAD"] = intercept
        manager.setup_command(env, hostfile, procs)
        self.lock.release()
        try:
            out = manager.run()
            self.lock.acquire(True)
            results[job_num] = IorCommand.get_ior_metrics(out)
            self.lock.release()
        except CommandFailure as error:
            self.log.error("IOR Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
