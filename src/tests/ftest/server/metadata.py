#!/usr/bin/python3
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import traceback
import uuid
import threading
import avocado
import queue
from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError
from ior_utils import IorCommand
from command_utils_base import CommandFailure
from job_manager_utils import Orterun

# Rough maximum number of containers that can be created
# Determined experimentally with DAOS_MD_CAP=128
# (ensure metadata.yaml matches)
# and taking into account vos reserving in vos_space_init():
#  5% for fragmentation overhead
#  ~ 1MB for background garbage collection use
#  ~48MB for background aggregation use
# 52% of remaining free space set aside for staging log container
# (installsnapshot RPC handling in raft)
#NO_OF_MAX_CONTAINER = 4150
NO_OF_MAX_CONTAINER = 3465


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
        super().__init__(*args, **kwargs)
        self.out_queue = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Create a pool
        self.add_pool(connect=False)
        self.log.info("Created pool %s: svcranks:",
                      self.pool.pool.get_uuid_str())
        for r in range(len(self.pool.svc_ranks)):
            self.log.info("[%d]: %d", r, self.pool.svc_ranks[r])

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

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=metadata,metadata_fill
        """
        # 3 Phases in nested try/except blocks below
        # Phase 1: overload pool metadata with a container create loop
        #          DaosApiError expected here (otherwise fail test)
        #
        # Phase 2: if Phase 1 passed:
        #          clean up all containers created (prove "critical" destroy
        #          in rdb (and vos) works without cascading nospace errors
        #
        # Phase 3: if Phase 2 passed:
        #          Sustained container create loop, eventually encountering
        #          -DER_NOSPACE, perhaps some successful creates (due to
        #          rdb log compaction, eventually settling into continuous
        #          -DER_NOSPACE. Make sure service keeps running.
        #
        self.pool.pool.connect(2)

        self.log.info("Phase 1: Fillup Metadata (expected to fail) ...")
        container_array = []
        try:
            # Phase 1 container creates
            for _cont in range(NO_OF_MAX_CONTAINER + 1000):
                container = DaosContainer(self.context)
                container.create(self.pool.pool.handle)
                container_array.append(container)

        # Phase 1 got DaosApiError (expected - proceed to Phase 2)
        except DaosApiError:
            self.log.info("Phase 1: passed (container create %d failed after "
                          "metadata full)", _cont)

            # Phase 2 clean up containers (expected to succeed)
            try:
                self.log.info("Phase 2: Cleaning up containers after "
                              "DaosApiError (expected to work)")
                for container in container_array:
                    container.destroy()
                self.log.info("Phase 2: pass (containers destroyed "
                              "successfully)")

                # Phase 3 sustained container creates even after nospace error
                # Due to rdb log compaction after initial nospace errors,
                # Some brief periods of available space will occur, allowing
                # a few container creates to succeed in the interim.
                self.log.info("Phase 3: sustained container creates: "
                              "to nospace and beyond")
                big_array = []
                in_failure = False
                for _cont in range(30000):
                    try:
                        container = DaosContainer(self.context)
                        container.create(self.pool.pool.handle)
                        big_array.append(container)
                        if in_failure:
                            self.log.info("Phase 3: nospace -> available "
                                          "transition, cont %d", _cont)
                            in_failure = False
                    except DaosApiError:
                        if not in_failure:
                            self.log.info("Phase 3: available -> nospace "
                                          "transition, cont %d", _cont)
                        in_failure = True

                self.log.info("Phase 3: passed (created %d / %d containers)",
                              len(big_array), 30000)
                return

            except DaosApiError as exe2:
                print(exe2, traceback.format_exc())
                self.fail("Phase 2: fail (unexpected container destroy error)")

        # Phase 1 failure
        self.fail("Phase 1: failed (metadata full error did not occur)")

    @avocado.fail_on(DaosApiError)
    def test_metadata_addremove(self):
        """JIRA ID: DAOS-1512.

        Test Description:
            Verify metadata release the space after container delete.

        Use Cases:
            ?

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,metadata,metadata_free_space
        """
        self.pool.pool.connect(2)
        for k in range(10):
            container_array = []
            self.log.info("Container Create Iteration %d / 9", k)
            for cont in range(NO_OF_MAX_CONTAINER):
                container = DaosContainer(self.context)
                try:
                    container.create(self.pool.pool.handle)
                except DaosApiError as exc:
                    self.log.info("Container create %d/%d failed: %s",
                                  cont, NO_OF_MAX_CONTAINER, exc)
                    self.fail("Container create failed")

                container_array.append(container)

            self.log.info("Created %d containers", (cont+1))
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

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,metadata,metadata_ior
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

    @avocado.fail_on(DaosApiError)
    def test_container_removal_after_der_nospace(self):
        """JIRA ID: DAOS-4858

        Test Description:
           Verify container can be successfully deleted when the storage pool
           is full ACL grant/remove modification.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,metadata,der_nospace,metadata_der_nospace
        """
        self.pool.pool.connect(2)
        init_container = NO_OF_MAX_CONTAINER
        additional_container = 1000000
        der_no_space = "RC: -1007"

        container_array = []
        self.log.info("(1)Start Creating %d containers..", init_container)
        for cont in range(init_container):
            container = DaosContainer(self.context)
            container.create(self.pool.pool.handle)
            container_array.append(container)

        self.log.info("(1.1)%d Container Created.", init_container)
        self.log.info("(1.2)Create additional 100 Containers, expect Fail.")

        #Create additional containers to check for DaosApiError -1007
        for cont in range(additional_container):
            try:
                container = DaosContainer(self.context)
                container.create(self.pool.pool.handle)
                container_array.append(container)
            except DaosApiError as exc:
                self.log.info("(1.3)Expected DaosApiError info: %s", exc)
                if der_no_space not in str(exc):
                    self.fail(
                        "##Expecting der_no_space RC: -1007, not seen")
                else:
                    self.log.info(traceback.format_exc())
                    self.log.info("(1.4)No space error shown with additional "
                                  "%d containers created, test passed.", cont)
                break
        if cont == additional_container - 1:
            self.fail(
                "##Storage resource did not exhausted after {} containers "
                "created".format(len(container_array)))
        self.log.info(
            "(1.5)Additional %d containers created and detected der_no_space.",
            cont)
        self.log.info(
            "(2)Verify %d Container Removal after storage full.. ",
            len(container_array))
        for cont in container_array:
            try:
                cont.destroy()
            except DaosApiError as exc:
                self.fail(
                    "##Container destroy failed after storage full.")
        self.log.info("(2.1)Container Removal succeed after storage full.")

        self.log.info(
            "(3)Create %d containers after container cleanup.",
            init_container)
        for cont in range(init_container):
            try:
                container = DaosContainer(self.context)
                container.create(self.pool.pool.handle)
                container_array.append(container)
            except DaosApiError as exc:
                self.log.info(traceback.format_exc())
                self.fail(
                    "##Failed to create containers after container cleanup.")
        self.log.info(
            "(3.1)Create %d containers succeed after cleanup.", init_container)
