#!/usr/bin/python3
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import traceback
import uuid
import threading
import queue

from avocado.core.exceptions import TestFail

from apricot import TestWithServers
from ior_utils import IorCommand
from command_utils_base import CommandFailure
from job_manager_utils import Orterun


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

    # Minimum number of containers that should be able to be created
    CREATED_CONTAINERS_MIN = 3000

    # Number of created containers that should not be possible
    CREATED_CONTAINERS_LIMIT = 3500

    def __init__(self, *args, **kwargs):
        """Initialize a ObjectMetadata object."""
        super().__init__(*args, **kwargs)
        self.out_queue = None

    def create_pool(self):
        """Create a pool and display the svc ranks."""
        self.add_pool()
        self.log.info("Created pool %s: svc ranks:", self.pool.uuid)
        for index, rank in enumerate(self.pool.svc_ranks):
            self.log.info("[%d]: %d", index, rank)

    def create_all_containers(self, expected=None):
        """Create the maximum number of supported containers.

        Args:
            expected (int, optional): number of containers expected to be
                created. Defaults to None.

        Returns:
            bool: True if all of the expected number of containers were created
                successfully; False otherwise

        """
        status = False
        self.container = []
        self.log.info(
            "Attempting to create up to %d containers",
            self.CREATED_CONTAINERS_LIMIT)
        for index in range(self.CREATED_CONTAINERS_LIMIT):
            # Continue to create containers until there is not enough space
            if not self._create_single_container(index):
                status = True
                break

        self.log.info(
            "Created %s containers before running out of space",
            len(self.container))

        # Safety check to avoid test timeout - should hit an exception first
        if len(self.container) >= self.CREATED_CONTAINERS_LIMIT:
            self.log.error(
                "Created too many containers: %d", len(self.container))

        # Verify that at least MIN_CREATED_CONTAINERS have been created
        if status and len(self.container) < self.CREATED_CONTAINERS_MIN:
            self.log.error(
                "Only %d containers created; expected %d",
                len(self.container), self.CREATED_CONTAINERS_MIN)
            status = False

        # Verify that the expected number of containers were created
        if status and expected and len(self.container) != expected:
            self.log.error(
                "Unexpected created container quantity: %d/%d",
                len(self.container), expected)
            status = False

        return status

    def _create_single_container(self, index):
        """Create a single container.

        Args:
            index (int): container count

        Returns:
            bool: was a container created successfully

        """
        status = False
        # self.log.info("Creating container %d", index + 1)
        self.container.append(self.get_container(self.pool, create=False))
        if self.container[-1].daos:
            self.container[-1].daos.verbose = False
        try:
            self.container[-1].create()
            status = True
        except TestFail as error:
            self.log.info(
                "  Failed to create container %s: %s",
                index + 1, str(error))
            del self.container[-1]
            if "RC: -1007" not in str(error):
                self.fail(
                    "Unexpected error detected creating container {}".format(
                        index + 1))
        return status

    def destroy_all_containers(self):
        """Destroy all of the created containers.

        Returns:
            bool: True if all of the containers were destroyed successfully;
                False otherwise

        """
        self.log.info("Destroying %d containers", len(self.container))
        errors = self.destroy_containers(self.container)
        if errors:
            self.log.error(
                "Errors detected destroying %d containers: %d",
                len(self.container), len(errors))
            for error in errors:
                self.log.error("  %s", error)
        self.container = []
        return len(errors) == 0

    def thread_control(self, threads, operation):
        """Start threads and wait until all threads are finished.

        Args:
            threads (list): list of threads to execute
            operation (str): IOR operation, e.g. "read" or "write"

        Returns:
            str: "PASS" if all threads completed successfully; "FAIL" otherwise

        """
        self.create_pool()
        self.d_log.debug("IOR {0} Threads Started -----".format(operation))
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()

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
        :avocado: tags=server,metadata,metadata_fillup
        """
        self.create_pool()

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

        # Phase 1 container creates
        self.log.info("Phase 1: Fill up Metadata (expected to fail) ...")
        if not self.create_all_containers():
            self.fail("Phase 1: failed (metadata full error did not occur)")
        self.log.info(
            "Phase 1: passed (container create %d failed after metadata full)",
            len(self.container) + 1)

        # Phase 2 clean up containers (expected to succeed)
        self.log.info(
            "Phase 2: Cleaning up %d containers (expected to work)",
            len(self.container))
        if not self.destroy_all_containers():
            self.fail("Phase 2: fail (unexpected container destroy error)")
        self.log.info("Phase 2: passed")

        # Phase 3 sustained container creates even after nospace error
        # Due to rdb log compaction after initial nospace errors, some brief
        # periods of available space will occur, allowing a few container
        # creates to succeed in the interim.
        self.log.info(
            "Phase 3: sustained container creates: to nospace and beyond")
        self.container = []
        sequential_fail_counter = 0
        sequential_fail_max = 1000
        in_failure = False
        for loop in range(30000):
            try:
                status = self._create_single_container(loop)

                # Keep track of the number of sequential no space container
                # create errors.  Once the max has been reached stop the loop.
                if status:
                    sequential_fail_counter = 0
                else:
                    sequential_fail_counter += 1
                if sequential_fail_counter >= sequential_fail_max:
                    self.log.info(
                        "Phase 3: container %d - %d/%d sequential no space "
                        "container create errors", sequential_fail_counter,
                        sequential_fail_max, loop)
                    break

                if status and in_failure:
                    self.log.info(
                        "Phase 3: container: %d - nospace -> available "
                        "transition, sequential no space failures: %d",
                        loop, sequential_fail_counter)
                    in_failure = False
                elif not status and not in_failure:
                    self.log.info(
                        "Phase 3: container: %d - available -> nospace "
                        "transition, sequential no space failures: %d",
                        loop, sequential_fail_counter)
                    in_failure = True

            except TestFail as error:
                self.log.error(str(error))
                self.fail("Phase 3: fail (unexpected container create error)")
        self.log.info(
            "Phase 3: passed (created %d / %d containers)",
            len(self.container), loop)
        self.log.info("Test passed")

    def test_metadata_addremove(self):
        """JIRA ID: DAOS-1512.

        Test Description:
            Verify metadata release the space after container delete.

        Use Cases:
            ?

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=server,metadata,metadata_compact,nvme,metadata_addremove
        """
        self.create_pool()
        self.container = []

        test_failed = False
        containers_created = []
        for loop in range(10):
            self.log.info("Container Create Iteration %d / 9", loop)
            if not self.create_all_containers():
                self.log.error("Errors during create iteration %d/9", loop)
                test_failed = True

            containers_created.append(len(self.container))

            self.log.info("Container Remove Iteration %d / 9", loop)
            if not self.destroy_all_containers():
                self.log.error("Errors during remove iteration %d/9", loop)
                test_failed = True

        self.log.info("Summary")
        self.log.info("  Loop  Containers Created  Variation")
        self.log.info("  ----  ------------------  ---------")
        sequential_negative_count = 0
        for loop, quantity in enumerate(containers_created):
            variation = 0
            if loop > 0:
                # Variations in the number of containers created is expected,
                # but make sure the number does not decrease more than 3 times
                # in a row.
                variation = quantity - containers_created[loop - 1]
                if variation < 0:
                    sequential_negative_count += 1
                    if sequential_negative_count > 2:
                        test_failed = True
                        variation = \
                            "{}  **FAIL: {} sequential decreases".format(
                                variation, sequential_negative_count)
                else:
                    sequential_negative_count = 0
            self.log.info("  %-4d  %-18d  %s", loop + 1, quantity, variation)

        if test_failed:
            self.fail("Errors verifying metadata space release")
        self.log.info("Test passed")

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
        :avocado: tags=server,metadata,metadata_ior,nvme
        """
        self.create_pool()
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
                    "Created %s thread %s with container uuids %s", operation,
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

        self.log.info("Test passed")

    def test_container_removal_after_der_nospace(self):
        """JIRA ID: DAOS-4858.

        Test Description:
           Verify container can be successfully deleted when the storage pool
           is full ACL grant/remove modification.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=server,metadata,metadata_der_nospace,nvme,der_nospace
        """
        self.create_pool()

        self.log.info("(1) Start creating containers..")
        if not self.create_all_containers():
            self.fail("Unexpected error creating containers")
        self.log.info("(1.1) %d containers created.", len(self.container))

        additional_containers = 1000000
        self.log.info(
            "(1.2) Create %d additional containers, expect fail.",
            additional_containers)
        for loop in range(additional_containers):
            try:
                self.container.append(self.get_container(self.pool))
            except TestFail as error:
                self.log.info("(1.3) Expected create failure: %s", error)
                if "RC: -1007" in str(error):
                    self.log.info(traceback.format_exc())
                    self.log.info(
                        "(1.4) No space error shown with additional %d "
                        "containers created, test passed.", loop)
                else:
                    self.fail("Expecting der_no_space 'RC: -1007' missing")
                break
        if loop == additional_containers - 1:
            self.fail(
                f"Storage resource not exhausted after {len(self.container)} "
                "containers created")
        self.log.info(
            "(1.5) Additional %d containers created and detected der_no_space.",
            loop)

        self.log.info(
            "(2) Verify removal of %d containers after full storage .. ",
            len(self.container))
        if not self.destroy_all_containers():
            self.fail("Container destroy failed after full storage.")
        self.log.info("(2.1) Container removal succeed after full storage.")

        self.log.info(
            "(3) Create %d containers after container cleanup.",
            len(self.container))
        if not self.create_all_containers(len(self.container)):
            self.fail("Failed to create containers after container cleanup.")
        self.log.info(
            "(3.1) Create %d containers succeed after cleanup.",
            len(self.container))

        self.log.info("Test passed")
