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
import time
import random
import uuid
import threading
import copy

from itertools import product
from avocado import fail_on
from apricot import TestWithServers, skipForTicket
from test_utils_pool import TestPool
from ior_utils import IorCommand
from job_manager_utils import Mpirun
from write_host_file import write_host_file
from command_utils import CommandFailure
from mpio_utils import MpioUtils
from daos_racer_utils import DaosRacerCommand

try:
    # python 3.x
    import queue as queue
except ImportError:
    # python 2.7
    import Queue as queue


class OSAOnlineParallelTest(TestWithServers):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server online drain,reintegration,
    extend test cases in parallel.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAOnlineParallelTest, self).setUp()
        self.dmg_command = self.get_dmg_command()
        self.no_of_dkeys = self.params.get("no_of_dkeys", '/run/dkeys/*')
        self.no_of_akeys = self.params.get("no_of_akeys", '/run/akeys/*')
        self.record_length = self.params.get("length", '/run/record/*')
        self.ior_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get("obj_class",
                                              '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.out_queue = queue.Queue()
        self.ds_racer_queue = queue.Queue()
        self.daos_racer = None

    @fail_on(CommandFailure)
    def get_pool_version(self):
        """Get the pool version.

        Returns:
            int: pool_version_value

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return int(data["version"])

    def daos_racer_thread(self, results):
        """Start the daos_racer thread.
        """
        self.daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0],
                                           self.dmg_command)
        self.daos_racer.get_params(self)
        self.daos_racer.set_environment(
            self.daos_racer.get_environment(self.server_managers[0]))
        self.daos_racer.run()
        results.put("Daos Racer Started")

    def ior_thread(self, pool, oclass, api, test, flags, results):
        """Start threads and wait until all threads are finished.
        Args:
            pool (object): pool handle
            oclass (str): IOR object class
            api (str): IOR api
            test (list): IOR test sequence
            flags (str): IOR flags
            results (queue): queue for returning thread results

        Returns:
            None
        """
        processes = self.params.get("slots", "/run/ior/clientslots/*")
        container_info = {}
        mpio_util = MpioUtils()
        if mpio_util.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test : Mpich not installed on :"
                      " {}".format(self.hostfile_clients[0]))
        self.pool = pool
        # Define the arguments for the ior_runner_thread method
        ior_cmd = IorCommand()
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(self.server_group, self.pool)
        ior_cmd.dfs_oclass.update(oclass)
        ior_cmd.api.update(api)
        ior_cmd.transfer_size.update(test[2])
        ior_cmd.block_size.update(test[3])
        ior_cmd.flags.update(flags)

        container_info["{}{}{}"
                       .format(oclass,
                               api,
                               test[2])] = str(uuid.uuid4())

        # Define the job manager for the IOR command
        manager = Mpirun(ior_cmd, mpitype="mpich")
        key = "".join([oclass, api, str(test[2])])
        manager.job.dfs_cont.update(container_info[key])
        env = ior_cmd.get_default_env(str(manager))
        manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        manager.assign_processes(processes)
        manager.assign_environment(env, True)

        # run IOR Command
        try:
            manager.run()
        except CommandFailure as _error:
            results.put("FAIL")

    def dmg_thread(self, action, action_args, results):
        """Generate different dmg command related to OSA.
            Args:
            action_args(dict) : {action: {"puuid":
                                          pool[val].uuid,
                                          "rank": rank,
                                          "target": t_string,
                                          "action": action,}
            results (queue) : dmg command output queue.
        """
        # Give sometime for IOR threads to start
        dmg = copy.copy(self.dmg_command)
        try:
            if action == "reintegrate":
                time.sleep(60)
            # For each action, read the values from the
            # dictionary.
            # example {"exclude" : {"puuid": self.pool, "rank": rank
            #                       "target": t_string, "action": exclude}}
            # getattr is used to obtain the method in dmg object.
            # eg: dmg -> pool_exclude method, then pass arguments like
            # puuid, rank, target to the pool_exclude method.
            getattr(dmg, "pool_{}".format(action))(**action_args[action])
        except CommandFailure as _error:
            results.put("{} failed".format(action_args[action]))
        # Future enhancement for extend
        # elif action == "extend":
        #    dmg.pool_extend(puuid, (rank + 2))

    def run_online_parallel_test(self, num_pool):
        """Run multiple OSA commands / IO in parallel.
            Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
        """
        num_jobs = self.params.get("no_parallel_job", '/run/ior/*')
        # Create a pool
        pool = {}
        pool_uuid = []
        target_list = []

        # Exclude target : random two targets  (target idx : 0-7)
        n = random.randint(0, 6)
        target_list.append(n)
        target_list.append(n+1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Exclude rank 2.
        rank = 2

        # Start the daos_racer thread
        kwargs = {"results": self.ds_racer_queue}
        daos_racer_thread = threading.Thread(target=self.daos_racer_thread,
                                             kwargs=kwargs)
        daos_racer_thread.start()
        time.sleep(30)

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context,
                                 dmg_command=self.get_dmg_command())
            pool[val].get_params(self)
            # Split total SCM and NVME size for creating multiple pools.
            pool[val].scm_size.value = int(pool[val].scm_size.value /
                                           num_pool)
            pool[val].nvme_size.value = int(pool[val].nvme_size.value /
                                            num_pool)
            pool[val].create()
            pool_uuid.append(pool[val].uuid)

        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)

            for oclass, api, test, flags in product(self.ior_dfs_oclass,
                                                    self.ior_apis,
                                                    self.ior_test_sequence,
                                                    self.ior_flags):
                threads = []
                # Action dictionary with OSA dmg command parameters
                action_args = {
                    "drain": {"pool": self.pool.uuid, "rank": rank,
                              "tgt_idx": None},
                    "exclude": {"pool": self.pool.uuid, "rank": (rank + 1),
                                "tgt_idx": t_string},
                    "reintegrate": {"pool": self.pool.uuid, "rank": (rank + 1),
                                    "tgt_idx": t_string}
                }
                for _ in range(0, num_jobs):
                    # Add a thread for these IOR arguments
                    threads.append(threading.Thread(target=self.ior_thread,
                                                    kwargs={"pool": pool[val],
                                                            "oclass": oclass,
                                                            "api": api,
                                                            "test": test,
                                                            "flags": flags,
                                                            "results":
                                                            self.out_queue}))
                for action in sorted(action_args):
                    # Add dmg threads
                    threads.append(threading.Thread(target=self.dmg_thread,
                                                    kwargs={"action": action,
                                                            "action_args":
                                                            action_args,
                                                            "results":
                                                            self.out_queue}))

                # Launch the IOR threads
                for thrd in threads:
                    self.log.info("Thread : %s", thrd)
                    thrd.start()
                    time.sleep(3)

                # Wait to finish the threads
                for thrd in threads:
                    thrd.join()

            # Check data consistency for IOR in future
            # Presently, we are running daos_racer in parallel
            # to IOR and checking the data consistency only
            # for the daos_racer objects after exclude
            # and reintegration.
            daos_racer_thread.join()

            for val in range(0, num_pool):
                display_string = "Pool{} space at the End".format(val)
                pool[val].display_pool_daos_space(display_string)
                fail_count = 0
                while fail_count <= 20:
                    pver_end = self.get_pool_version()
                    time.sleep(10)
                    fail_count += 1
                    if pver_end > 23:
                        break
                self.log.info("Pool Version at the End %s", pver_end)
                self.assertTrue(pver_end == 25,
                                "Pool Version Error:  at the end")
                pool[val].destroy()

    @skipForTicket("DAOS-5877")
    def test_osa_online_parallel_test(self):
        """
        JIRA ID: DAOS-4752

        Test Description: Runs multiple OSA commands/IO in parallel

        :avocado: tags=all,pr,hw,large,osa,osa_parallel,online_parallel
        """
        self.run_online_parallel_test(1)
