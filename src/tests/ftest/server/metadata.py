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
from __future__ import print_function

import os
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
from agent_utils import run_agent, stop_agent
from pydaos.raw import DaosContainer, DaosApiError
from ior_utils import IorCommand
from command_utils import CommandFailure
from job_manager_utils import Orterun
from server_utils import run_server, stop_server
from test_utils_pool import TestPool

NO_OF_MAX_CONTAINER = 13034


def ior_runner_thread(manager, uuids, results):
    """IOR run thread method.

    Args:
        manager (str): mpi job manager command
        uuids (list): [description]
        results (queue): queue for returning thread results
    """
    for index, cont_uuid in enumerate(uuids):
        manager.job.daos_cont.update(cont_uuid, "ior.cont_uuid")
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

    @skipForTicket("DAOS-1936/DAOS-1946")
    def test_metadata_fillup(self):
        """JIRA ID: DAOS-1512.

        Test Description:
            Test to verify no IO happens after metadata is full.

        Use Cases:
            ?

        :avocado: tags=all,metadata,large,metadatafill,hw
        :avocado: tags=full_regression
        """
        self.pool.pool.connect(2)
        container = DaosContainer(self.context)

        self.log.info("Fillup Metadata....")
        for _cont in range(NO_OF_MAX_CONTAINER):
            container.create(self.pool.pool.handle)

        # This should fail with no Metadata space Error.
        self.log.info("Metadata Overload...")
        try:
            for _cont in range(400):
                container.create(self.pool.pool.handle)
            self.fail("Test expected to fail with a no metadata space error")

        except DaosApiError as exe:
            print(exe, traceback.format_exc())
            return

        self.fail("Test was expected to fail but it passed.\n")

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
                path = os.path.join(self.ompi_prefix, "bin")
                manager = Orterun(ior_cmd, path)
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
                # Stop the agents and servers
                if self.agent_sessions:
                    stop_agent(self.agent_sessions, self.hostlist_clients)
                stop_server(hosts=self.hostlist_servers)

                # Start the agents
                self.agent_sessions = run_agent(
                    self, self.hostlist_clients,
                    self.hostlist_servers)

                # Start the servers
                run_server(
                    self, self.hostfile_servers, self.server_group,
                    clean=False)
