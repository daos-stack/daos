"""
  (C) Copyright 2019-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from exception_utils import CommandFailure
from ior_utils import IorCommand
from job_manager_utils import get_job_manager
from thread_manager import ThreadManager


def run_ior_loop(test, manager, loops):
    """Run IOR multiple times.

    Args:
        test (Test): the test object
        manager (str): mpi job manager command
        loops (int): number of times to run IOR

    Returns:
        list: a list of CmdResults from each ior command run

    """
    results = []
    errors = []
    for index in range(loops):
        cont = test.label_generator.get_label('cont')
        manager.job.dfs_cont.update(cont, "ior.dfs_cont")

        t_start = time.time()

        try:
            results.append(manager.run())
        except CommandFailure as error:
            ior_mode = "read" if "-r" in manager.job.flags.value else "write"
            errors.append(
                "IOR {} Loop {}/{} failed for container {}: {}".format(
                    ior_mode, index, loops, cont, error))
        finally:
            t_end = time.time()
            ior_cmd_time = t_end - t_start
            results.append("ior_cmd_time = {}".format(ior_cmd_time))

    if errors:
        raise CommandFailure(
            "IOR failed in {}/{} loops: {}".format(len(errors), loops, "\n".join(errors)))
    return results


class ObjectMetadata(TestWithServers):
    """Test class for metadata testing.

    Test Class Description:
        Test the general Metadata operations and boundary conditions.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super().__init__(*args, **kwargs)
        self.ior_managers = []

        # Minimum number of containers that should be able to be created
        self.created_containers_min = self.params.get("created_cont_min", "/run/metadata/*")

        # Number of created containers that should not be possible
        self.created_containers_limit = self.params.get("created_cont_max", "/run/metadata/*")

    def pre_tear_down(self):
        """Tear down steps to optionally run before tearDown().

        Returns:
            list: a list of error strings to report at the end of tearDown().

        """
        error_list = []
        if self.ior_managers:
            self.test_log.info("Stopping IOR job managers")
            error_list = self._stop_managers(self.ior_managers, "IOR job manager")
        else:
            self.log.debug("no pre-teardown steps defined")
        return error_list

    def create_pool(self, svc_ops_enabled=True):
        """Create a pool and display the svc ranks.

        Args:
            svc_ops_enabled (Bool, optional): pool create with svc_ops_enabled, default to True.

        """
        if svc_ops_enabled:
            self.add_pool()
        else:
            params = {}
            params['properties'] = "svc_ops_enabled:0"
            self.add_pool(**params)
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
            self.created_containers_limit)
        for index in range(self.created_containers_limit):
            # Continue to create containers until there is not enough space
            if not self._create_single_container(index):
                status = True
                break

        self.log.info(
            "Created %s containers before running out of space",
            len(self.container))

        # Safety check to avoid test timeout - should hit an exception first
        if len(self.container) >= self.created_containers_limit:
            self.log.error(
                "Created too many containers: %d", len(self.container))

        # Verify that at least MIN_CREATED_CONTAINERS have been created
        if status and len(self.container) < self.created_containers_min:
            self.log.error(
                "Only %d containers created; expected %d",
                len(self.container), self.created_containers_min)
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
        self.container.append(self.get_container(self.pool, create=False))
        if self.container[-1].daos:
            self.container[-1].daos.verbose = False
        try:
            self.container[-1].create()
            return True
        except TestFail as error:
            self.log.info("  Failed to create container %s: %s", index + 1, str(error))
            del self.container[-1]
            if "RC: -1007" not in str(error):
                self.fail("Unexpected error detected creating container {}".format(index + 1))
        return False

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

    def destroy_num_containers(self, num):
        """Destroy some of the created containers.

        Args:
            num (int): Number of containers to destroy

        Returns:
            bool: True if all of the containers were destroyed successfully;
                False otherwise

        """
        num = min(num, len(self.container))
        self.log.info("Destroying %d containers", num)
        errors = self.destroy_containers(self.container[0:num])
        if errors:
            self.log.error("Errors detected destroying %d containers: %d", num, len(errors))
            for error in errors:
                self.log.error("  %s", error)
        self.container = []
        return len(errors) == 0

    def run_dummy_metadata_workload(self, duration=150):
        """Run some container metadata operations for a specified time duration.

        Args:
            duration (int): amount of time in seconds to run the workload

        Returns:
            bool: True if all calls successful; False otherwise

        """
        self.container = []
        if not self._create_single_container(0):
            return False

        # Populate the pool rdb svc_ops KVS with some metadata operation history
        # svc_ops uses some rdb storage and impacts amount of user-visible metadata resources
        self.log.info("Start dummy workload (open/close) for %d seconds", duration)
        t_start = time.time()
        t_delta = 0
        while t_delta < duration:
            if not self.container[-1].open():
                return False
            if not self.container[-1].close():
                return False
            t_delta = time.time() - t_start
        self.log.info("Done dummy workload loop")

        if not self.destroy_all_containers():
            return False

        return True

    def metadata_fillup(self, svc_ops_enabled=True):
        """Run test to verify no IO happens after metadata is full.

        Args:
            svc_ops_enabled (bool): Pool create properties svc_ops_enabled, default to True.

        """
        self.create_pool(svc_ops_enabled=svc_ops_enabled)
        # Skip dummy_metadata_workload when feature is disabled
        if svc_ops_enabled:
            svc_ops_entry_age = self.pool.get_property("svc_ops_entry_age")
            if not self.run_dummy_metadata_workload(duration=svc_ops_entry_age):
                self.fail("failed to run dummy metadata workload")
        sequential_fail_max = self.params.get("fillup_seq_fail_max", "/run/metadata/*")
        num_cont_to_destroy = self.params.get("num_cont_to_destroy", "/run/metadata/*")

        # 2 Phases
        # Phase 1: Sustained container create loop, eventually encountering
        #          -DER_NOSPACE, perhaps some successful creates (due to
        #          rdb log compaction, eventually settling into continuous
        #          -DER_NOSPACE. Make sure service keeps running.
        #
        # Phase 2: if Phase 1 passed:
        #          clean up several (not all) containers created (prove "critical" destroy
        #          in rdb (and vos) works without cascading no space errors

        # Phase 1 sustained container creates even after no space error
        self.log.info(
            "Phase 1: sustained container creates: to no space and beyond")
        self.container = []
        sequential_fail_counter = 0
        in_failure = False
        for loop in range(self.created_containers_limit + 1000):
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
                        "Phase 1: container %d - %d/%d sequential no space "
                        "container create errors", sequential_fail_counter,
                        sequential_fail_max, loop)
                    break

                if status and in_failure:
                    self.log.info(
                        "Phase 1: container: %d - no space -> available "
                        "transition, sequential no space failures: %d",
                        loop, sequential_fail_counter)
                    in_failure = False
                elif not status and not in_failure:
                    self.log.info(
                        "Phase 1: container: %d - available -> no space "
                        "transition, sequential no space failures: %d",
                        loop, sequential_fail_counter)
                    in_failure = True

            except TestFail as error:
                self.log.error(str(error))
                self.fail("Phase 1: fail (unexpected container create error)")
        if len(self.container) >= self.created_containers_limit:
            self.log.error("Phase 1: Created too many containers: %d > %d", len(self.container),
                           self.created_containers_limit)
            self.fail("Phase 1: Created too many containers")
        if len(self.container) < self.created_containers_min:
            self.log.info("Phase 1: Created too few containers: %d < %d", len(self.container),
                          self.created_containers_min)
            self.fail("Phase 1: Created too few containers")
        self.log.info(
            "Phase 1: passed (created %d / %d containers)", len(self.container), loop)

        # Phase 2 clean up some containers (expected to succeed)
        self.log.info("Phase 2: Cleaning up %d containers (expected to work)", num_cont_to_destroy)
        if not self.destroy_num_containers(num_cont_to_destroy):
            self.fail("Phase 2: fail (unexpected container destroy error)")
        self.log.info("Phase 2: passed")

        # Do not destroy containers in teardown (destroy pool while metadata rdb is full)
        self.container = None
        self.log.info("Leaving pool metadata rdb full (containers will not be destroyed)")
        self.log.info("Test passed")

    def test_metadata_fillup_svc_ops_disabled(self):
        """JIRA ID: DAOS-15628.

        Test Description:
            Test to verify no IO happens after metadata is full when svc_ops_disabled.

        Use Cases:
            Create pool with properties svc_ops_enabled:0.
            Create container until no space.
            Verify number of container within limit.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=server,metadata
        :avocado: tags=ObjectMetadata,test_metadata_fillup_svc_ops_disabled
        """
        self.metadata_fillup(False)

    def test_metadata_fillup_svc_ops_enabled(self):
        """JIRA ID: DAOS-15628.

        Test Description:
            Test to verify no IO happens after metadata is full when svc_ops_enabled.

        Use Cases:
            Create pool with properties svc_ops_enabledd:1.
            Run dummy metadata workload to fill up svc ops.
            Create container until no space.
            Verify number of container within limit.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=server,metadata
        :avocado: tags=ObjectMetadata,test_metadata_fillup_svc_ops_enabled
        """
        self.metadata_fillup(True)

    def test_metadata_addremove(self):
        """JIRA ID: DAOS-1512.

        Test Description:
            Verify metadata release the space after container delete.

        Use Cases:
            ?

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=server,metadata,nvme
        :avocado: tags=ObjectMetadata,test_metadata_addremove
        """
        self.create_pool()
        # Skip dummy_metadata_workload when feature is disabled
        svc_ops_enabled = self.pool.get_property("svc_ops_enabled")
        if svc_ops_enabled:
            svc_ops_entry_age = self.pool.get_property("svc_ops_entry_age")
            if not self.run_dummy_metadata_workload(duration=svc_ops_entry_age):
                self.fail("failed to run dummy metadata workload")
        self.container = []
        mean_cont_cnt = 0
        percent_cont = self.params.get("mean_percent", "/run/metadata/*")
        num_loops = self.params.get("num_addremove_loops", "/run/metadata/*")

        test_failed = False
        containers_created = []
        for loop in range(num_loops):
            self.log.info("Container Create Iteration %d / %d", loop, num_loops - 1)
            # The test will encounter ENOSPACE (-1007) while creating containers.
            if not self.create_all_containers():
                self.log.error("Errors during create iteration %d/%d", loop, num_loops - 1)

            containers_created.append(len(self.container))
            # We should make sure containers which are created should
            # be destroyed without issues. Here we have to check for
            # any container destroy errors.
            self.log.info("Container Remove Iteration %d / %d", loop, num_loops - 1)
            if not self.destroy_all_containers():
                self.log.error("Errors during remove iteration %d/%d", loop, num_loops - 1)
                test_failed = True
        # Calculate the mean container count
        mean_cont_cnt = sum(containers_created) / len(containers_created)
        percent_from_mean = int(mean_cont_cnt * (percent_cont / 100))
        self.log.info(
            "Mean number of containers created in %s loops: %s",
            len(containers_created), mean_cont_cnt)
        self.log.info(
            "Max variation in number of containers from %s%% from mean: %s",
            percent_cont, percent_from_mean)

        self.log.info("Summary")
        self.log.info("  Loop  Containers Created  Variation")
        self.log.info("  ----  ------------------  ---------")
        for loop, quantity in enumerate(containers_created):
            variation = 0
            if loop > 0:
                # Variation in the number of containers created
                # should be less than %x from mean containers created
                variation = abs(mean_cont_cnt - containers_created[loop - 1])
                if variation > percent_from_mean:
                    test_failed = True
                    variation = \
                        "{}  **FAIL: {} variation failure from mean".format(
                            variation, percent_from_mean)
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
        :avocado: tags=server,metadata,nvme,ior
        :avocado: tags=ObjectMetadata,test_metadata_server_restart
        """
        self.create_pool()
        files_per_thread = 400
        total_ior_threads = 5

        processes = self.params.get("slots", "/run/ior/clientslots/*")

        # Launch threads to run IOR to write data, restart the agents and
        # servers, and then run IOR to read the data
        for operation in ("write", "read"):
            # Setup the thread manager
            thread_manager = ThreadManager(run_ior_loop, self.timeout - 30)

            # Create the IOR threads
            for index in range(total_ior_threads):
                # Define the arguments for the run_ior_loop method
                ior_cmd = IorCommand()
                ior_cmd.get_params(self)
                ior_cmd.set_daos_params(self.server_group, self.pool, None)
                ior_cmd.flags.value = self.params.get("ior{}flags".format(operation), "/run/ior/*")

                # Define the job manager for the IOR command
                self.ior_managers.append(
                    get_job_manager(self, "Clush", ior_cmd))
                env = ior_cmd.get_default_env(str(self.ior_managers[-1]))
                self.ior_managers[-1].assign_hosts(self.hostlist_clients, self.workdir, None)
                self.ior_managers[-1].assign_processes(processes)
                self.ior_managers[-1].assign_environment(env)
                self.ior_managers[-1].verbose = False

                # Add a thread for these IOR arguments
                thread_manager.add(
                    test=self, manager=self.ior_managers[-1], loops=files_per_thread)
                self.log.info("Created %s thread %s", operation, index)

            # Launch the IOR threads
            self.log.info("Launching %d IOR %s threads", thread_manager.qty, operation)
            failed_thread_count = thread_manager.check_run()
            self.log.info("Done %d IOR %s threads", thread_manager.qty, operation)
            if failed_thread_count > 0:
                msg = "{} FAILED IOR {} Thread(s)".format(failed_thread_count, operation)
                self.d_log.error(msg)
                self.fail(msg)

            # Restart the agents and servers after the write / before the read
            if operation == "write":
                # Stop the agents
                errors = self.stop_agents()
                self.assertEqual(
                    len(errors), 0,
                    "Error stopping agents:\n  {}".format("\n  ".join(errors)))

                # Restart the servers w/o formatting the storage
                errors = self.restart_servers()
                self.assertEqual(
                    len(errors), 0,
                    "Error stopping servers:\n  {}".format("\n  ".join(errors)))

                # Start the agents
                self.start_agent_managers()

        self.log.info("Test passed")
