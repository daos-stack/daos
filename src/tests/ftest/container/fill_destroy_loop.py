"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from general_utils import DaosTestError, bytes_to_human, human_to_bytes
from run_utils import run_remote


class BoundaryPoolContainerSpace(TestWithServers):
    """Test class for pool Boundary testing.

    Test Class Description:
        Test to create a pool and container and write random data to fill the container and delete
        container, repeat the test 100 times as boundary condition.

    :avocado: recursive
    """

    DER_NOSPACE = "-1007"

    def __init__(self, *args, **kwargs):
        """Initialize a BoundaryPoolContainerSpace object."""
        super().__init__(*args, **kwargs)

        self.test_loop = 0
        self.reclaim_props = []
        self.delta_bytes = 0

    def setUp(self):
        """Set up each test case."""
        super().setUp()

        self.test_loop = self.params.get("test_loop", "/run/pool/*", 0)
        self.reclaim_props = self.params.get("reclaim_props", "/run/pool/*", [])

        delta = self.params.get("delta", "/run/pool/*", "0")
        self.delta_bytes = human_to_bytes(delta)
        self.log.info("==> Set pool delta to %s (%i bytes)", delta, self.delta_bytes)

    def check_server_logs(self):
        """Check if GC engine errors have occurred during the test

        This method parse the engine log to find silent errors regarding the GC engine operations.
        When the SCM storage is completely full, it is not possible to perform some operations on
        metadata such as destroying a pool.  Indeed, such operations require some temporary SCM
        storage to reorganize the B-trees used for recording the data layout of the pools.  When
        such error occurs, some storage leakage could eventually happen.  At this time, the only
        reliable way to detect these errors is to check if errors regarding B-tree management have
        occurred.  With this test, this can be done with looking for ENOSPACE error messages
        generated with the call of the function gc_reclaim_pool().
        """
        self.log.info("==>Checking server logs of hosts %s", self.hostlist_servers)

        err_regex = r"'^.+[[:space:]]+vos[[:space:]]+CRIT.+[[:space:]]vos_gc_pool_tight\(\) "
        err_regex += r"gc_reclaim_pool failed DER_NOSPACE.+$'"
        log_dir = os.path.dirname(self.server_managers[0].get_config_value("log_file"))

        cmd = "find {} -type f -regextype egrep ".format(log_dir)
        cmd += r"-regex '.*/daos_server\.log\.[[:digit:]]+' "
        cmd += r"-exec grep -q -E -e " + err_regex + r" {} ';' -print"
        result = run_remote(self.log, self.hostlist_servers, cmd)

        self.assertTrue(
            result.passed,
            "Unexpected errors occurred while processing server log files on hosts {}"
            .format(result.failed_hosts))

        hosts = [host for host, stdout in result.all_stdout.items() if stdout]
        self.assertEqual(
            0, len(hosts),
            "Unexpected errors occurred during garbage collection on hosts {}".format(hosts))

    def write_pool_until_nospace(self, test_loop):
        """write pool and container until pool is full.

        Args:
            test_loop (int): test loop for log info.
        """
        # Create a container and get pool free space before write
        container = self.get_container(self.pool)
        free_space_init = self.pool.get_pool_free_space()
        self.log.info("--%i.(3)Pool free space before writing data to container %s (%i bytes)",
                      test_loop, bytes_to_human(free_space_init), free_space_init)

        # Write random data to container until pool out of space
        base_data_size = container.data_size.value
        data_written = 0
        while True:
            new_data_size = self.random.randint(base_data_size * 0.5, base_data_size * 1.5)  # nosec
            container.data_size.update(new_data_size, "data_size")

            try:
                container.write_objects()
            except DaosTestError as excep:
                if self.DER_NOSPACE in str(excep):
                    self.log.info(
                        "--%i.(4)DER_NOSPACE %s detected, pool is unable for an additional"
                        " %s (%i bytes) object", test_loop, self.DER_NOSPACE,
                        bytes_to_human(container.data_size.value), container.data_size.value)
                    break
                self.fail("Test-loop {0} exception while writing object: {1}".format(
                    test_loop, repr(excep)))
            data_written += new_data_size

        # display free space and data written
        free_space_before_destroy = self.pool.get_pool_free_space()
        self.log.info(
            "--%i.(5) %s (%i bytes) written when pool is full.",
            test_loop, bytes_to_human(data_written), data_written)

        # display free space stats after destroy
        container.destroy()
        free_space_after_destroy = self.pool.get_pool_free_space()
        self.log.info(
            "--%i.(6)Pool full, free space before container delete %s (%i bytes)",
            test_loop, bytes_to_human(free_space_before_destroy), free_space_before_destroy)
        self.log.info(
            "--%i.(7)Pool full, free space after container deleted %s (%i bytes)",
            test_loop, bytes_to_human(free_space_after_destroy), free_space_after_destroy)

        # sanity checks on free space
        self.assertGreater(
            free_space_after_destroy, free_space_before_destroy,
            "Deleting container did not free up pool space: "
            "loop={}, before={} ({} bytes), end={} ({} bytes)".format(
                test_loop, bytes_to_human(free_space_before_destroy), free_space_before_destroy,
                bytes_to_human(free_space_after_destroy), free_space_after_destroy))
        self.assertAlmostEqual(
            free_space_init, free_space_after_destroy, delta=self.delta_bytes,
            msg="Deleting container did not restore all free pool space: "
            "loop={}, init={} ({} bytes), end={} ({} bytes)".format(
                test_loop, bytes_to_human(free_space_init), free_space_init,
                bytes_to_human(free_space_after_destroy), free_space_after_destroy))
        self.log.debug(
            "Storage space leaked %s (%i bytes)",
            bytes_to_human(abs(free_space_init - free_space_after_destroy)),
            free_space_init - free_space_after_destroy)

    def test_fill_destroy_cont_loop(self):
        """JIRA ID: DAOS-8465

        Test Description:
            Purpose of the test is to stress pool and container space usage
            boundary, test by looping of container object_write until pool full, check for
            any other error.

        Use Case:
            - Create Pool
            - repeat following steps:
              (0) Set reclaim pool property
              (1) Create Container.
              (2) Fill the pool with random block data size, verify return code is as expected
                  when no more data can be written to the container.
              (3) Measure free space before writing data to container
              (4) Check for DER_NOSPACE -1007.
              (5) Display bytes written when pool is full before container delete.
              (6) Display free space before container delete.
              (7) Display and verify free space after container delete.
            - Check server logs

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container,pool,boundary_test
        :avocado: tags=BoundaryPoolContainerSpace,test_fill_destroy_cont_loop
        """
        # create pool
        self.add_pool()

        for test_loop in range(1, self.test_loop + 1):
            self.log.info("==>Starting test loop: %i ...", test_loop)

            if self.reclaim_props:
                reclaim_prop = self.reclaim_props.pop(0)
                self.log.info(
                    '--%i.(0)Set Pool reclaim properties to "%s"',
                    test_loop, reclaim_prop)
                self.pool.set_property("reclaim", reclaim_prop)

            self.pool.set_query_data()
            self.log.info(
                "--%i.(1)Query pool %s before write: %s",
                test_loop, str(self.pool), self.pool.query_data)
            free_space = self.pool.get_pool_free_space()
            self.log.info(
                "--%s.(2)Pool free space before container create: %s (%i bytes)",
                test_loop, bytes_to_human(free_space), free_space)

            self.write_pool_until_nospace(test_loop)

        self.check_server_logs()
