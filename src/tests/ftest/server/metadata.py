#!/usr/bin/python
"""
  (C) Copyright 2019-2020 Intel Corporation.

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

import traceback
import uuid
import threading
import avocado

try:
    # python 3.x
    import queue
except ImportError:
    # python 2.7
    import Queue as queue

from apricot import TestWithServers, skipForTicket
from pydaos.raw import DaosContainer, DaosApiError
from ior_utils import IorCommand
from command_utils_base import CommandFailure
from job_manager_utils import Orterun
from test_utils_pool import TestPool

# Rough maximum number of containers that can be created
# Determined experimentally with DAOS_MD_CAP=128
# (ensure metadata.yaml matches)
# and taking into account vos reserving in vos_space_init():
#  5% for fragmentation overhead
#  ~ 1MB for background garbage collection use
#  ~48MB for background aggregation use
# 52% of remaining free space set aside for staging log container
# (installsnapshot RPC handling in raft)
NO_OF_MAX_CONTAINER = 4464

def ior_runner_thread(manager, uuids, results):
    """IOR run thread method.

    Args:
        manager (str): mpi job manager command
        uuids (list): [description]
        results (queue): queue for returning thread results
    """
    for index, cont_uuid in enumerate(uuids):
        manager.job.dfs_cont.update(cont_uuid, "ior.cont_uuid")
        try:
            manager.run()
        except CommandFailure as error:
            print(
                "--- FAIL --- Thread-{0} Failed to run IOR {1}: "
                "Exception {2}".format(
                    index,
                    "read" if "-r" in manager.job.flags.value else "write",
                    str(error)))
            results.put("FAIL")


class ObjectMetadata(TestWithServers):
    """Test class for metadata testing.

    Test Class Description:
        Test the general Metadata operations and boundary conditions.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ObjectMetadata object."""
        super(ObjectMetadata, self).__init__(*args, **kwargs)
        self.out_queue = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(ObjectMetadata, self).setUp()

        # Create a pool
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.create()

    def thread_control(self, threads, operation):
        """Start threads and wait until all threads are finished.

        Args:
            threads (list): list of threads to execute
            operation (str): IOR operation, e.g. "read" or "write"

        Returns:
            str: "PASS" if all threads completed successfully; "FAIL" otherwise

        """
        self.d_log.debug("IOR {0} Threads Started -----".format(operation))
        for thrd in threads:
            thrd.start()
        for thrd in threads:
            thrd.join()

        while not self.out_queue.empty():
            if self.out_queue.get() == "FAIL":
                return "FAIL"
        self.d_log.debug("IOR {0} Threads Finished -----".format(operation))
        return "PASS"

    def test_metadata_fillup(self):
        """JIRA ID: DAOS-1512.

        Test Description:
            Test to verify no IO happens after metadata is full.

        Use Cases:
            ?

        :avocado: tags=all,metadata,large,metadatafill,hw
        :avocado: tags=full_regression
        """

        # 3 Phases in nested try/except blocks below
        # Phase 1: nearly fill pool metadata with container creates
        #          no DaosApiError expected in this phase (otherwise fail test)
        #
        # Phase 2: if Phase 1 passed:
        #          overload pool metadata with another container create loop
        #          DaosApiError IS expected here (otherwise fail test)
        #
        # Phase 3: if Phase 2 passed:
        #          clean up all containers created (prove "critical" destroy
        #          in rdb (and vos) works without cascading nospace errors

        self.pool.pool.connect(2)

        self.log.info("Phase 1: Fillup Metadata (expected to work) ...")
        container_array = []
        try:
            # Phase 1 container creates
            for _cont in range(NO_OF_MAX_CONTAINER):
                container = DaosContainer(self.context)
                container.create(self.pool.pool.handle)
                container_array.append(container)

            self.log.info("Phase 1: pass (all container creates successful)")

            # Phase 2: should fail with no Metadata space Error.
            try:
                self.log.info("Phase 2: Metadata Overload (expect to fail) ...")
                for _cont in range(400):
                    container = DaosContainer(self.context)
                    container.create(self.pool.pool.handle)
                    container_array.append(container)

                # Phase 2 failed - stop here
                self.fail("Phase 2: fail (expected container create failure)")

            # Phase 2 DaosApiError (expected - proceed to Phase 3)
            except DaosApiError:
                self.log.info("Phase 2: pass (container create failed as "
                              "expected)")

                # Phase 3 clean up containers (expected to succeed)
                try:
                    self.log.info("Phase 3: Cleaning up containers after "
                                  "DaosApiError (expected to work)")
                    for container in container_array:
                        container.destroy()
                    self.log.info("Phase 3: pass (containers destroyed "
                                  "successfully)")
                    return

                except DaosApiError as exe3:
                    print(exe3, traceback.format_exc())
                    self.fail("Phase 3: fail (container destroy error)")

        # Phase 1 got DaosApiError (not expected)
        except DaosApiError as exe1:
            print(exe1, traceback.format_exc())
            self.fail("Phase 1: failure (container creates should have worked)")


    @avocado.fail_on(DaosApiError)
    def test_metadata_addremove(self):
        """JIRA ID: DAOS-1512.

        Test Description:
            Verify metadata release the space after container delete.

        Use Cases:
            ?

        :avocado: tags=metadata,metadata_free_space,nvme,large,hw
        :avocado: tags=full_regression
        """
        self.pool.pool.connect(2)
        for k in range(10):
            container_array = []
            self.log.info("Container Create Iteration %d / 9", k)
            for cont in range(NO_OF_MAX_CONTAINER):
                container = DaosContainer(self.context)
                container.create(self.pool.pool.handle)
                container_array.append(container)

            self.log.info("Container Remove Iteration %d / 9", k)
            for cont in container_array:
                cont.destroy()

    @avocado.fail_on(DaosApiError)
    def test_metadata_server_restart(self):
        """JIRA ID: DAOS-1512.

        Test Description:
            This test will verify 2000 IOR small size container after server
            restart. Test will write IOR in 5 different threads for faster
            execution time. Each thread will create 400 (8bytes) containers to
            the same pool. Restart the servers, read IOR container file written
            previously and validate data integrity by using IOR option
            "-R -G 1".

        Use Cases:
            ?

        :avocado: tags=metadata,metadata_ior,nvme,large
        """
        files_per_thread = 400
        total_ior_threads = 5
        self.out_queue = queue.Queue()

        processes = self.params.get("slots", "/run/ior/clientslots/*")

        list_of_uuid_lists = [
            [str(uuid.uuid4()) for _ in range(files_per_thread)]
            for _ in range(total_ior_threads)]

        # Launch threads to run IOR to write data, restart the agents and
        # servers, and then run IOR to read the data
        for operation in ("write", "read"):
            # Create the IOR threads
            threads = []
            for index in range(total_ior_threads):
                # Define the arguments for the ior_runner_thread method
                ior_cmd = IorCommand()
                ior_cmd.get_params(self)
                ior_cmd.set_daos_params(self.server_group, self.pool)
                ior_cmd.flags.value = self.params.get(
                    "F", "/run/ior/ior{}flags/".format(operation))

                # Define the job manager for the IOR command
                manager = Orterun(ior_cmd)
                env = ior_cmd.get_default_env(str(manager))
                manager.assign_hosts(self.hostlist_clients, self.workdir, None)
                manager.assign_processes(processes)
                manager.assign_environment(env)

                # Add a thread for these IOR arguments
                threads.append(
                    threading.Thread(
                        target=ior_runner_thread,
                        kwargs={
                            "manager": manager,
                            "uuids": list_of_uuid_lists[index],
                            "results": self.out_queue}))

                self.log.info(
                    "Creatied %s thread %s with container uuids %s", operation,
                    index, list_of_uuid_lists[index])

            # Launch the IOR threads
            if self.thread_control(threads, operation) == "FAIL":
                self.d_log.error("IOR {} Thread FAIL".format(operation))
                self.fail("IOR {} Thread FAIL".format(operation))

            # Restart the agents and servers after the write / before the read
            if operation == "write":
                # Stop the agents
                errors = self.stop_agents()
                self.assertEqual(
                    len(errors), 0,
                    "Error stopping agents:\n  {}".format("\n  ".join(errors)))

                # Stop the servers
                errors = self.stop_servers()
                self.assertEqual(
                    len(errors), 0,
                    "Error stopping servers:\n  {}".format("\n  ".join(errors)))

                # Start the agents
                self.start_agent_managers()

                # Start the servers
                self.start_server_managers()
