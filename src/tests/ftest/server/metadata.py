#!/usr/bin/python
'''
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
'''
from __future__ import print_function

import traceback
import uuid
import threading
import Queue
import avocado

from apricot import TestWithServers, skipForTicket
from agent_utils import run_agent, stop_agent
from daos_api import DaosContainer, DaosApiError
from ior_utils import IorCommand, IorFailed
from server_utils import run_server, stop_server
from write_host_file import write_host_file
from test_utils import TestPool

NO_OF_MAX_CONTAINER = 13180


def ior_runner_thread(ior_cmd, uuids, mgr, attach, procs, hostfile, results):
    """IOR run thread method.

    Args:
        ior_cmd (IorCommand): [description]
        uuids (list): [description]
        mgr (str): mpi job manager command
        attach (str): CART attach info path
        procs (int): number of host processes
        hostfile (str): file defining host names and slots
        results (Queue): queue for returning thread results
    """
    for index, cont_uuid in enumerate(uuids):
        ior_cmd.daos_cont.update(cont_uuid, "ior.cont_uuid")
        try:
            ior_cmd.run(mgr, attach, procs, hostfile, False)
            results.put("PASS")

        except IorFailed as error:
            print(
                "--- FAIL --- Thread-{0} Failed to run IOR {1}: "
                "Exception {2}".format(
                    index, "read" if "-r" in ior_cmd.flags.value else "write",
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

        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)

        # Create a pool
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.create()

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.pool is not None:
                self.pool.destroy(1)
        finally:
            super(ObjectMetadata, self).tearDown()

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

        :avocado: tags=all,metadata,pr,small,metadatafill
        """
        self.pool.pool.connect(2)
        container = DaosContainer(self.context)

        self.d_log.debug("Fillup Metadata....")
        for _cont in range(NO_OF_MAX_CONTAINER):
            container.create(self.pool.pool.handle)

        # This should fail with no Metadata space Error.
        self.d_log.debug("Metadata Overload...")
        try:
            for _cont in range(250):
                container.create(self.pool.pool.handle)
            self.fail("Test expected to fail with a no metadata space error")

        except DaosApiError as exe:
            print(exe, traceback.format_exc())
            return

        self.fail("Test was expected to fail but it passed.\n")

    @skipForTicket("DAOS-1965")
    @avocado.fail_on(DaosApiError)
    def test_metadata_addremove(self):
        """JIRA ID: DAOS-1512.

        Test Description:
            Verify metadata release the space after container delete.

        Use Cases:
            ?

        :avocado: tags=metadata,metadata_free_space,nvme,small
        """
        self.pool.pool.connect(2)
        for k in range(10):
            container_array = []
            self.d_log.debug("Container Create Iteration {}".format(k))
            for cont in range(NO_OF_MAX_CONTAINER):
                container = DaosContainer(self.context)
                container.create(self.pool.pool.handle)
                container_array.append(container)

            self.d_log.debug("Container Remove Iteration {} ".format(k))
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

        :avocado: tags=metadata,metadata_ior,nvme,small
        """
        files_per_thread = 400
        total_ior_threads = 5
        self.out_queue = Queue.Queue()

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

                # Add a thread for these IOR arguments
                threads.append(
                    threading.Thread(
                        target=ior_runner_thread,
                        kwargs={
                            "ior_cmd": ior_cmd,
                            "uuids": list_of_uuid_lists[index],
                            "mgr": self.orterun,
                            "attach": self.tmp,
                            "hostfile": self.hostfile_clients,
                            "procs": processes,
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
                    self.basepath, self.hostlist_clients,
                    self.hostlist_servers)

                # Start the servers
                run_server(
                    self.hostfile_servers, self.server_group, self.basepath,
                    clean=False)
