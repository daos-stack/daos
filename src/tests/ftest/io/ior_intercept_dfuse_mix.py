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
from ior_utils import IorCommand, IorMetrics
from command_utils import CommandFailure


class IorInterceptMultiClient(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR only with dfuse and with mix of
       dfuse and interception library on a single server and multi
       client settings with basic parameters.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super(IorInterceptMultiClient, self).setUp()
        # Running ior on multi client for transfer size under 4K has issues
        # The IorTestBase filters the test clients and uses only 1 client
        # for all the test variants. Since this test is going to run
        # only on 4K, removing that constraint and allowing all the
        # clients to run ior. This set up can be removed once the constraint
        # in IorTestBase is removed. # DAOS-3320
        if self.ior_cmd.api.value == "POSIX":
            self.hostlist_clients = self.params.get("test_clients", "/run/hosts/*")
        self.lock = threading.Lock()

    def test_ior_intercept_multi_client(self):
        """Jira ID: DAOS-3500.

        Test Description:
            Purpose of this test is to run ior through dfuse on 4 clients
            for 5 minutes and capture the metrics and use the
            intercepiton library by exporting LD_PRELOAD to the libioil.so
            path on 3 clients and leave 1 client to use dfuse and rerun
            the above ior and capture the metrics and compare the
            performance difference and check using interception
            library make significant performance improvement. Verify the
            client didn't use the interception library doesn't show any
            improvement.

        Use case:
            Run ior with read, write for 5 minutes
            Run ior with read, write for 5 minutes with interception
            library

            Compare the results and check whether using interception
                library provides better performance and not using it
                does not change the performance.

        :avocado: tags=all,daosio,hw,full_regression,iorinterceptmix
        """
        without_intercept = dict()
        self.run_ior_with_pool(without_intercept)
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        with_intercept = dict()
        self.run_ior_with_pool(with_intercept, intercept)
        self.log_metrics(without_intercept, with_intercept)

        max_mib = int(IorMetrics.Max_MiB)
        min_mib = int(IorMetrics.Min_MiB)
        mean_mib = int(IorMetrics.Mean_MiB)

        write_x = self.params.get("write_x", "/run/ior/iorflags/ssf/*", 1)
        read_x = self.params.get("read_x", "/run/ior/iorflags/ssf/*", 1)

        # Verify that using interception library gives desired performance
        # improvement.
        # Verifying write performance
        self.assertTrue(float(with_intercept[1][0][max_mib]) >
                        write_x * float(without_intercept[1][0][max_mib]))
        self.assertTrue(float(with_intercept[1][0][min_mib]) >
                        write_x * float(without_intercept[1][0][min_mib]))
        self.assertTrue(float(with_intercept[1][0][mean_mib]) >
                        write_x * float(without_intercept[1][0][mean_mib]))

        # Verifying read performance
        self.assertTrue(float(with_intercept[1][1][max_mib]) >
                        read_x * float(without_intercept[1][1][max_mib]))
        self.assertTrue(float(with_intercept[1][1][min_mib]) >
                        read_x * float(without_intercept[1][1][min_mib]))
        self.assertTrue(float(with_intercept[1][1][mean_mib]) >
                        read_x * float(without_intercept[1][1][mean_mib]))

        # Verify that not using interception library on both runs does
        # not change the performance.
        # Perf. improvement if any is less than the desired.
        # Verifying write performance
        self.assertTrue(float(with_intercept[2][0][max_mib]) <
                        write_x * float(without_intercept[2][0][max_mib]))
        self.assertTrue(float(with_intercept[2][0][min_mib]) <
                        write_x * float(without_intercept[2][0][min_mib]))
        self.assertTrue(float(with_intercept[2][0][mean_mib]) <
                        write_x * float(without_intercept[2][0][mean_mib]))

        # Verifying read performance
        # Read performance is not significant with interception library
        # and most likely the read_x will be 1. To avoid unnecessary
        # failure keeping flat 1.5 x just to set the boundary for the client
        # without interception library
        self.assertTrue(float(with_intercept[2][1][max_mib]) <
                        1.5 * float(without_intercept[2][1][max_mib]))
        self.assertTrue(float(with_intercept[2][1][min_mib]) <
                        1.5 * float(without_intercept[2][1][min_mib]))
        self.assertTrue(float(with_intercept[2][1][mean_mib]) <
                        1.5 * float(without_intercept[2][1][mean_mib]))

    def log_metrics(self, without_intercept, with_intercept):
        """Log the ior metrics because the stdout from ior can be mixed
           because of multithreading.

           Args:
               without_intercept (dict): IOR Metrics without using
                                         interception library.
               with_intercept (dict): IOR Metrics using interception
                                      library.
        """
        IorCommand.log_metrics(self.log, "3 clients - without " +
                               "interception library", without_intercept[1])
        IorCommand.log_metrics(self.log, "3 clients - with " +
                               "interception library", with_intercept[1])
        IorCommand.log_metrics(self.log, "1 client - without " +
                               "interception library", without_intercept[2])
        IorCommand.log_metrics(self.log, "1 clients - without " +
                               "interception library", with_intercept[2])

    def run_ior_with_pool(self, results, intercept=None):
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
            if self.ior_cmd.transfer_size.value == "256B":
                self.cancelForTicket("DAOS-3449")
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
        job = threading.Thread(target=self.run_ior, args=(
            hostfile, len(clients), results, job_num, intercept))
        return job

    def run_ior(self, hostfile, num_clients, results, job_num, intercept=None):
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
