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
from test_utils_pool import TestPool
from test_utils_container import TestContainer

from general_utils import DaosTestError
from avocado.core.exceptions import TestError, TestFail

def ior_runner_thread(manager, uuids, results):
    """IOR run thread method.

    Args:
        manager (Orterun): mpi job manager command
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
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        self.pool.create()
        self.log.info("Created pool %s: svcranks:",
                      self.pool.pool.get_uuid_str())
        for r in range(len(self.pool.svc_ranks)):
            self.log.info("[%d]: %d", r, self.pool.svc_ranks[r])
        self.pool.pool.connect(2)

    def get_pool_leader(self, pool=None):
        """ Get the pool leader

        Args:
            pool (TestPoll): Pool object to query
        Returns:
            str: Value of leader
        """
        if not pool:
            pool = self.pool
        pool.set_query_data()
        return pool.query_data["response"]["leader"]

    def get_pool_rebuild_status(self, pool=None):
        """ Get the pool leader

        Args:
            pool (TestPool): Pool object to query
        Returns:
            str: Value of leader
        """
        if not pool:
            pool = self.pool
        pool.set_query_data()
        return pool.query_data["response"]["rebuild"]["state"]

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

    def create_containers_until_error(self, pool=None):
        """Creates containers in pool

        Args:
            pool (TestPool): Pool where the containers will be created.

        Returns:
            set: List with all DaosContainer(s) created and length

        """
        # Phase 1 container creates
        if not pool:
            pool = self.pool
        der_no_space = "RC: -1007"
        container_array = []
        self.log.info("Filling up Metadata until error,"
                      "this may take a while ...")
        while True:
            try:
                container = DaosContainer(self.context)
                container.create(pool.pool.handle)
                container_array.append(container)
            except DaosApiError as exception:
                self.log.info("Expected DaosApiError info: %s", exception)
                if der_no_space not in str(exception):
                    self.fail(
                        "##Expecting der_no_space RC: -1007, not seen")
                else:
                    container_length = len(container_array)
                    self.log.info("Container create #%d failed. "
                                  "Metadata full)", container_length)
                break

        return container_array, container_length

    def delete_containers_in_array(self, container_array):
        """Creates containers in pool

        Args:
            container_array (List): List of DaosContainer instances

        Returns:
            list: If all containers are destroyed correctly, an empty list
                  otherwise an exception is raised
        Raises:
            DaosApiError
        """
        self.log.info("Cleaning up containers after ")
        for cont in range(len(container_array)):
            container_array[cont].destroy()
            container_array.pop(cont)

        self.log.info("Containers destroyed successfully")
        return container_array

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

    def test_metadata_fillup(self):
        """JIRA ID: DAOS-1512.
        Test Description:
            Test to verify no IO happens after metadata is full.

        3 Phases in nested try/except blocks below
        Phase 1: overload pool metadata with a container create loop
                 DaosApiError expected here (otherwise fail test)

        Phase 2: if Phase 1 passed:
                 clean up all containers created (prove "critical" destroy
                 in rdb (and vos) works without cascading nospace errors

        Phase 3: if Phase 2 passed:
                 Sustained container create loop, eventually encountering
                 -DER_NOSPACE, perhaps some successful creates (due to
                 rdb log compaction, eventually settling into continuous
                 -DER_NOSPACE. Make sure service keeps running.


        Use Cases:
            ?
        :avocado: tags=all,metadata,large,metadatafill,hw
        :avocado: tags=full_regression
        """

        self.log.info("Phase 1:")
        container_array, container_length = self.create_containers_until_error()

        try:
            # Phase 2, destroy all containers
            self.log.info("Phase 2:")
            self.delete_containers_in_array(container_array)

            self.log.info("Phase 3:")
            self.log.info("Sustained container creates: "
                          "to no-space and beyond.")
            big_array = []
            in_failure = False
            phase_3_status = False
            extra_container_length = int(container_length * 1.5)
            for _cont in range(extra_container_length):
                try:
                    container = DaosContainer(self.context)
                    container.create(self.pool.pool.handle)
                    big_array.append(container)
                    if in_failure:
                        self.log.info("Phase 3: nospace -> available "
                                      "transition, cont %d", _cont)
                        in_failure = False
                        phase_3_status = True
                except DaosApiError:
                    if not in_failure:
                        self.log.info("Phase 3: available -> nospace "
                                      "transition, cont %d", _cont)
                    in_failure = True

            len_big_array = len(big_array)

            if phase_3_status:
                self.log.info("Phase 3: passed (created %d / %d containers)",
                              len_big_array, container_length)
            else:
                # Cleaning array to avoid spam in debug information
                big_array = []
                self.fail("Phase 3: failed, unable to create containers "
                          "after NO SPACE ERROR,"
                          "No rdb log compaction?"
                          "(created {} / {} containers)".format(len_big_array,
                                                                container_length
                                                                ))
        except DaosApiError as exe2:
            print(exe2, traceback.format_exc())
            self.fail("Phase 2: fail (unexpected container destroy error)")

    @avocado.fail_on(DaosApiError)
    def test_metadata_fillup_cycle_same_leadership(self):
        """JIRA ID: DAOS-1512 & DAOS-4894

        Test Description:
            Verify metadata release the space after container delete.
            Verify pool leader does not change within the cycle.

        :avocado: tags=all,full_regression
        :avocado: tags=nvme,large,hw
        :avocado: tags=metadata
        :avocado: tags=metadata_no_leader_change
        """
        for k in range(10):
            self.log.info("Container create/delete cycle %d / 9", k)
            pool_leader = self.get_pool_leader()
            self.log.info("Current leader: %s", pool_leader)

            container_array, _ = self.create_containers_until_error()

            if pool_leader != self.get_pool_leader():
                self.fail("Leader unexpectedly changed after creating conts.")

            self.delete_containers_in_array(container_array)

            if pool_leader != self.get_pool_leader():
                self.fail("Leader unexpectedly changed after deleting conts.")
        self.log.info("Test passed, no error found within create/delete cycle.")

    def test_metadata_pool_replica_leadership_change_no_space(self):
        """
        Test description:
            Verify a pool leadership changes correctly when metadata is
            filled up.

        Test steps:
            Create a pool with storage replica(s) (using --nranks).
            Query pool and save rank leader.
            Fill metadata until DER_NOSPACE.
            Change leadership (at the moment it should be done by stopping
            the server that has the leader rank, for easy use, use 1 rank per
            server).
            Wait for rebuild to start -> complete.
            Verify the pool is still serviceable:
                Query pool
                Attempt to add containers until DER_NOSPACE
                Delete containers
                Add containers again

        :avocado: tags=all,full_regression
        :avocado: tags=small,hw
        :avocado: tags=metadata
        :avocado: tags=metadata_leader_change_no_space
        """
        dmg = self.get_dmg_command()
        pool_leader = self.get_pool_leader()
        self.log.info("Current leader: %s", pool_leader)

        container_array, container_length = self.create_containers_until_error()

        dmg.system_stop(force=True, ranks=pool_leader)

        # Wait for pool rebuild
        if self.get_pool_rebuild_status == "idle":
            self.pool.wait_for_rebuild(to_start=True)
        self.pool.wait_for_rebuild(to_start=False)

        # Query pool happens here
        new_pool_leader = self.get_pool_leader()
        self.log.info("New pool leader: %s", new_pool_leader)

        # Attempt to create containers until error
        container_array_less_ranks, container_length_less_ranks = \
            self.create_containers_until_error()
        self.log.info("Number of extra containers created: %s",
                      container_length_less_ranks)

        # Delete containers and fill again until error
        self.delete_containers_in_array(container_array)
        self.delete_containers_in_array(container_array_less_ranks)
        container_array, container_length = self.create_containers_until_error()
        self.log.info("Containers created: %d. with one less rank"
                      "metadata full)", container_length)

        # Restore back the original system
        dmg.system_start(ranks=pool_leader)

    def test_metadata_pool_replica_leadership_change_space_free(self):
        """
        Test description:
            Verify a pool leadership changes correctly when metadata is
            not completely filled up.

        Test steps:
            Create a pool with storage replica(s) (using --nranks).
            Query pool and save rank leader.
            Fill metadata until DER_NOSPACE
            Delete 10% of total containers
            Change leadership (at the moment it should be done by stopping
            the server that has the leader rank, for easy use, use 1 rank per
            server).
            Wait for rebuild to start -> complete.
            Verify the pool is still serviceable:
                Query pool
                Attempt to add containers until DER_NOSPACE
                Delete containers
                Add containers again

        :avocado: tags=all,full_regression
        :avocado: tags=small,hw
        :avocado: tags=metadata_bug_with_debug
        :avocado: tags=metadata_leader_change_space
        """
        test_result = True
        dmg = self.get_dmg_command()
        pool_leader = self.get_pool_leader()
        self.log.info("Current leader: %s", pool_leader)

        container_array, container_length = self.create_containers_until_error()

        self.log.info("Deleting 50% of created containers")
        percent = int(container_length * 0.50)
        for cont in range(percent):
            container_array[cont].destroy()
            container_array.pop(cont)
        self.log.info("Deleted 50% of containers")
        self.log.info("Number of containers: %s", len(container_array))
        dmg.system_stop(force=True, ranks=pool_leader)

        from time import sleep
        sleep(28800)  # Sleep for 8 hours

        for attempt in range(5):
            try:
                if self.get_pool_rebuild_status == "idle":
                    self.pool.wait_for_rebuild(to_start=True)
                self.pool.wait_for_rebuild(to_start=False)
            except DaosTestError as exception:
                # Unable to exit from the rebuild loop
                self.log.error("***********DAOS TEST ERROR*************")
                self.log.error("%s", exception)
                test_result = False
            except CommandFailure as exception:
                self.log.error("***********COMMAND FAILURE*************")
                # Failed to execute a command within rebuild
                self.log.error("%s", exception)
                test_result = False
            except DaosApiError as exception:
                self.log.error("***** DAOS API ERROR *****")
                self.log.error("%s", type(exception))
                test_result = False
            except TestError as exception:
                self.log.error("********************TEST ERROR*****")
                self.log.error("%s", type(exception))
                test_result = False
            except TestFail as exception:
                self.log.error("********************TEST FAIL*****")
                self.log.error("%s", type(exception))
                test_result = False
            except Exception as exception:
                self.log.error("********************FAIL UNKNOWN*****")
                self.log.error("%s", type(exception))
                test_result = False
            else:
                self.log.info("Rebuild complete!")
                break

        if not test_result:
            # Clean variables to avoid spam in variable values
            self.log.error("********************TEST FINALLY*****")
            container_array = []
            # Restore ranks
            dmg.system_start(ranks=pool_leader)
            pool_leader = self.get_pool_leader()
            # Fail test
            self.fail("Test failed.")

        # Query pool
        new_pool_leader = self.get_pool_leader()
        # Uncharted territory, what would be the correct behaviour
        # Expect to be able to create containers?
        container_array_less_ranks = self.create_containers_until_error()
        container_length_less_ranks = len(container_array_less_ranks)
        self.log.info("Number of extra containers created: %s",
                      container_length_less_ranks)
        failed_containers = 0
        try:
            for container in container_array:
                container.destroy()
        except DaosApiError as exception:
            self.log.warning("%s", exception)
            self.log.warning("Failed to delete cont")
            failed_containers += 1
        if failed_containers != 0:
            self.log.error("Failed to delete this many %s cont",
                           str(failed_containers))
        container_array = self.create_containers_until_error()
        container_length = len(container_array)
        self.log.info("Containers created: %d. with one less rank"
                      "metadata full)", container_length)
        dmg.system_start(ranks=pool_leader)
