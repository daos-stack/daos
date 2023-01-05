"""
  (C) Copyright 2021-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from random import choice
from re import findall

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet

from general_utils import get_avocado_config_value
from run_utils import run_remote, run_local, RunException
from test_utils_pool import POOL_TIMEOUT_INCREMENT
from user_utils import get_chown_command


class HarnessAdvancedTest(TestWithServers):
    """Advanced harness test cases.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super().__init__(*args, **kwargs)

        # Always start the servers for each test variant
        self.start_agents_once = False
        self.start_servers_once = False

    def test_core_files(self):
        """Test to verify core file creation.

        This test will send a signal 6 to a random daos_engine process so
        that it will create a core file, allowing the core file collection code
        in launch.py to be tested.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,core_files
        :avocado: tags=HarnessAdvancedTest,test_core_files
        """
        # create a core.gdb file
        self.log.debug("Create a core.gdb.harness.advanced file in core_pattern dir.")
        try:
            results = run_local(self.log, "cat /proc/sys/kernel/core_pattern", check=True)
        except RunException:
            self.fail("Unable to find local core file pattern")
        core_path = os.path.split(results.stdout.splitlines()[-1])[0]
        core_file = "{}/core.gdb.harness.advanced".format(core_path)

        self.log.debug("Creating %s", core_file)
        try:
            with open(core_file, "w", encoding="utf-8") as local_core_file:
                local_core_file.write("THIS IS JUST A TEST\n")
        except IOError as error:
            self.fail("Error writing {}: {}".format(local_core_file, str(error)))

        # Choose a server find the pid of its daos_engine process
        host = NodeSet(choice(self.server_managers[0].hosts))   # nosec
        ranks = self.server_managers[0].get_host_ranks(host)
        self.log.info("Obtaining pid of the daos_engine process on %s (rank %s)", host, ranks)
        pid = None
        result = run_remote(self.log, host, "pgrep --list-full daos_engine", timeout=20)
        if not result.passed:
            self.fail("Error obtaining pid of the daos_engine process on {}".format(host))
        pid = findall(r"(\d+)\s+[A-Za-z0-9/]+daos_engine\s+", "\n".join(result.output[0].stdout))[0]
        if pid is None:
            self.fail("Error obtaining pid of the daos_engine process on {}".format(host))
        self.log.info("Found pid %s", pid)

        # Send a signal 6 to its daos_engine process
        self.log.info("Sending a signal 6 to %s", pid)
        if not run_remote(self.log, host, "sudo -n kill -6 {}".format(pid)).passed:
            self.fail("Error sending a signal 6 to {} on {}".format(pid, host))

        # Simplify resolving the host name to rank by marking all ranks as
        # expected to be either running or errored (sent a signal 6)
        self.server_managers[0].update_expected_states(ranks, ["Joined", "Errored"])

        # Wait for the engine to create the core file
        ranks = self.server_managers[0].get_host_ranks(host)
        state = ["errored"]
        try:
            self.log.info(
                "Waiting for the engine on %s (rank %s) to move to the %s state",
                host, ranks, state)
            if self.server_managers[0].check_rank_state(ranks, state, 25):
                self.fail("Rank {} state not {} after sending signal 6".format(ranks, state))
        finally:
            # Display the journalctl log for the process that was sent the signal
            self.server_managers[0].manager.dump_logs(host)

        self.log.info("Test passed")

    def test_core_files_hw(self):
        """Test to verify core file creation.

        This test will send a signal 6 to a random daos_engine process so
        that it will create a core file, allowing the core file collection code
        in launch.py to be tested.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness,core_files
        :avocado: tags=HarnessAdvancedTest,test_core_files_hw
        """
        self.test_core_files()

    def test_pool_timeout(self):
        """Test to verify tearDown() timeout setting for timed out tests.

        Adding a pool should increase the runner.timeout.after_interrupted,
        runner.timeout.process_alive, and runner.timeout.process_died timeouts by 200 seconds each.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,pool_timeout
        :avocado: tags=HarnessAdvancedTest,test_pool_timeout
        """
        namespace = "runner.timeout"
        timeouts = {"after_interrupted": [], "process_alive": [], "process_died": []}

        self.log.info("Before creating pools:")
        for key in sorted(timeouts):
            timeouts[key].append(get_avocado_config_value(namespace, key))
            self.log.info("    %s.%s = %s", namespace, key, timeouts[key][0])

        self.add_pool(create=True, connect=True)
        self.add_pool(create=True, connect=False)
        self.add_pool(create=False)

        self.log.info("After creating pools:")
        for key in sorted(timeouts):
            timeouts[key].append(get_avocado_config_value(namespace, key))
            self.log.info("    %s.%s = %s", namespace, key, timeouts[key][1])

        for key in sorted(timeouts):
            self.assertEqual(
                int(timeouts[key][1]) - int(timeouts[key][0]), POOL_TIMEOUT_INCREMENT * 3,
                "Incorrect {}.{} value detected after adding 3 pools".format(namespace, key))

        self.log.info("Test passed")

    def test_pool_timeout_hw(self):
        """Test to verify tearDown() timeout setting for timed out tests.

        Adding a pool should increase the runner.timeout.after_interrupted,
        runner.timeout.process_alive, and runner.timeout.process_died timeouts by 200 seconds each.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness,pool_timeout
        :avocado: tags=HarnessAdvancedTest,test_pool_timeout_hw
        """
        self.test_pool_timeout()

    def test_launch_failures(self):
        """Test to verify launch.py post processing error reporting.

        The test will place uniquely named files in the paths where launch.py archives test results.
        When these files are detected launch.py will trigger a fake failure for the archiving of
        each file.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,launch_failures,failure_expected
        :avocado: tags=HarnessAdvancedTest,test_launch_failures
        """
        host = NodeSet(choice(self.server_managers[0].hosts))   # nosec
        self.log.info("Creating launch.py failure trigger files on %s", host)
        failure_trigger = "00_trigger-launch-failure_00"
        failure_trigger_dir = os.path.join(self.base_test_dir, failure_trigger)
        failure_trigger_files = [
            os.path.join(self.base_test_dir, "{}_local.yaml".format(failure_trigger)),
            os.path.join(os.sep, "etc", "daos", "daos_{}.yml".format(failure_trigger)),
            os.path.join(self.base_test_dir, "{}.log".format(failure_trigger)),
            os.path.join(failure_trigger_dir, "{}.log".format(failure_trigger)),
            os.path.join(os.sep, "tmp", "daos_dump_{}.txt".format(failure_trigger)),
            os.path.join(self.tmp, "valgrind_{}".format(failure_trigger)),
        ]

        self.log.debug("Creating %s", failure_trigger_dir)
        commands = [
            "sudo -n mkdir -p {}".format(failure_trigger_dir),
            "sudo -n {}".format(get_chown_command(options='-R', file=failure_trigger_dir)),
        ]

        local_trigger_file = failure_trigger_files.pop(0)
        self.log.debug("Creating %s", local_trigger_file)
        try:
            with open(local_trigger_file, "w", encoding="utf-8") as local_trigger:
                local_trigger.write("THIS IS JUST A TEST\n")
        except IOError as error:
            self.fail("Error writing {}: {}".format(local_trigger_file, str(error)))

        for command in commands:
            if not run_remote(self.log, host, command, timeout=20).passed:
                self.fail("Error creating directory {}".format(failure_trigger_dir))

        for failure_trigger_file in failure_trigger_files:
            self.log.debug("Creating %s", failure_trigger_file)
            sudo = "" if failure_trigger_file.startswith(self.tmp) else "sudo -n "
            commands = [
                "{}touch {}".format(sudo, failure_trigger_file),
                "{}{}".format(sudo, get_chown_command(options='-R', file=failure_trigger_file)),
                "echo 'THIS IS JUST A TEST' > {}".format(failure_trigger_file),
            ]
            for command in commands:
                if not run_remote(self.log, host, command, timeout=20).passed:
                    self.fail("Error creating file {}".format(failure_trigger_file))

    def test_launch_failures_hw(self):
        """Test to verify launch.py post processing error reporting.

        The test will place uniquely named files in the paths where launch.py archives test results.
        When these files are detected launch.py will trigger a fake failure for the archiving of
        each file.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness,launch_failures,failure_expected
        :avocado: tags=HarnessAdvancedTest,test_launch_failures_hw
        """
        self.test_launch_failures()
