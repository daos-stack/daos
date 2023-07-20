'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time
import threading

from avocado.core.exceptions import TestFail

from apricot import skipForTicket
from nvme_utils import ServerFillUp
from daos_utils import DaosCommand
from job_manager_utils import get_job_manager
from ior_utils import IorCommand, IorMetrics
from exception_utils import CommandFailure
from general_utils import error_count


class NvmeEnospace(ServerFillUp):
    """
    Test Class Description: To validate DER_NOSPACE for SCM and NVMe
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a NvmeEnospace object."""
        super().__init__(*args, **kwargs)
        self.daos_cmd = None

    def setUp(self):
        """Initial setup"""
        super().setUp()

        # initialize daos command
        self.daos_cmd = DaosCommand(self.bin)
        self.create_pool_max_size()
        self.der_nospace_count = 0
        self.other_errors_count = 0
        self.test_result = []

    def verify_enospace_log(self, der_nospace_err_count):
        """
        Function to verify there are no other error except DER_NOSPACE and
        DER_NO_HDL in client log.Verify DER_NOSPACE count is higher.

        args:
            expected_err_count(int): Expected DER_NOSPACE count from client log.
        """
        # Get the DER_NOSPACE and other error count from log
        self.der_nospace_count, self.other_errors_count = error_count(
            "-1007", self.hostlist_clients, self.client_log)

        # Get the DER_NO_HDL and other error count from log
        der_nohdl_count, other_nohdl_err = error_count(
            "-1002", self.hostlist_clients, self.client_log)

        # Check there are no other errors in log file except DER_NO_HDL
        if self.other_errors_count != der_nohdl_count:
            self.fail('Found other errors, count {} in client log {}'
                      .format(int(self.other_errors_count - other_nohdl_err),
                              self.client_log))
        # Check the DER_NOSPACE error count is higher if not test will FAIL
        if self.der_nospace_count < der_nospace_err_count:
            self.fail('Expected DER_NOSPACE should be > {} and Found {}'
                      .format(der_nospace_err_count, self.der_nospace_count))

    def delete_all_containers(self):
        """
        Delete all the containers.
        """
        # List all the container
        kwargs = {"pool": self.pool.uuid}
        data = self.daos_cmd.container_list(**kwargs)
        containers = [uuid_label["uuid"] for uuid_label in data["response"]]

        # Destroy all the containers
        for _cont in containers:
            kwargs["cont"] = _cont
            kwargs["force"] = True
            self.daos_cmd.container_destroy(**kwargs)

    def ior_bg_thread(self, event):
        """Start IOR Background thread, This will write small data set and
        keep reading it in loop until it fails or main program exit.

        args:
            event(obj): Event indicator to stop IOR read.
        """

        # Define the IOR Command and use the parameter from yaml file.
        ior_bg_cmd = IorCommand()
        ior_bg_cmd.get_params(self)
        ior_bg_cmd.set_daos_params(self.server_group, self.pool, None)
        ior_bg_cmd.dfs_oclass.update(self.ior_cmd.dfs_oclass.value)
        ior_bg_cmd.api.update(self.ior_cmd.api.value)
        ior_bg_cmd.transfer_size.update(self.ior_scm_xfersize)
        ior_bg_cmd.block_size.update(self.ior_cmd.block_size.value)
        ior_bg_cmd.flags.update(self.ior_cmd.flags.value)
        ior_bg_cmd.test_file.update('/testfile_background')

        # Define the job manager for the IOR command
        job_manager = get_job_manager(self, job=ior_bg_cmd)

        # create container
        container = self.get_container(self.pool)

        job_manager.job.dfs_cont.update(container.uuid)
        env = ior_bg_cmd.get_default_env(str(job_manager))
        job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        job_manager.assign_processes(1)
        job_manager.assign_environment(env, True)
        self.log.info('----Run IOR in Background-------')
        # run IOR Write Command
        try:
            job_manager.run()
        except (CommandFailure, TestFail):
            self.test_result.append("FAIL ior write")
            return

        # run IOR Read Command in loop
        ior_bg_cmd.flags.update(self.ior_read_flags)
        stop_looping = False
        while not stop_looping:
            try:
                job_manager.run()
            except (CommandFailure, TestFail):
                self.test_result.append("FAIL - ior read")
                break
            stop_looping = event.wait(1)

    def run_enospace_foreground(self):
        """Run IOR to fill up SCM and NVMe. Verify that we see DER_NOSPACE while filling
        up SCM. Then verify that the storage usage is near 100%.
        """
        # Fill 75% of current SCM free space. Aggregation is Enabled so NVMe space will
        # start to fill up.
        self.log.info('Starting main IOR load')
        self.start_ior_load(storage='SCM', operation="Auto_Write", percent=75)
        self.log.info(self.pool.pool_percentage_used())

        # Fill 50% of current SCM free space. Aggregation is Enabled so NVMe space will
        # continue to fill up.
        self.start_ior_load(storage='SCM', operation="Auto_Write", percent=50)
        self.log.info(self.pool.pool_percentage_used())

        # Fill 60% of current SCM free space. This time, NVMe will be Full so data will
        # not be moved to NVMe and continue to fill up SCM. SCM will be full and this
        # command is expected to fail with DER_NOSPACE.
        try:
            self.start_ior_load(storage='SCM', operation="Auto_Write", percent=60)
            self.fail('This test is suppose to FAIL because of DER_NOSPACE'
                      'but it Passed')
        except TestFail:
            self.log.info('Test is expected to fail because of DER_NOSPACE')

        # Display the pool usage %
        self.log.info(self.pool.pool_percentage_used())

        # verify the DER_NO_SAPCE error count is expected and no other Error in client log
        self.verify_enospace_log(self.der_nospace_count)

        # Check both NVMe and SCM are full.
        pool_usage = self.pool.pool_percentage_used()
        # NVMe should be almost full. If not, fail the test.
        if pool_usage['nvme'] <= 95:
            msg = (f"Pool NVMe used percentage should be > 95%, instead "
                   f"{pool_usage['nvme']}")
            self.fail(msg)
        # SCM usage will not be 100% because some space (<1%) is used for the system.
        if pool_usage['scm'] <= 95:
            msg = f"Pool SCM used percentage should be > 95%, instead {pool_usage['scm']}"
            self.fail(msg)

    def run_enospace_with_bg_job(self):
        """
        Function to run test and validate DER_ENOSPACE and expected storage
        size. Single IOR job will run in background while space is filling.
        """
        # Get the initial DER_ENOSPACE count
        self.der_nospace_count, self.other_errors_count = error_count(
            "-1007", self.hostlist_clients, self.client_log)

        # Start the IOR Background thread which will write small data set and
        # read in loop, until storage space is full.
        job = threading.Thread(target=self.ior_bg_thread)
        stop_ior_read = threading.Event()
        job = threading.Thread(target=self.ior_bg_thread, args=[stop_ior_read])
        job.daemon = True
        job.start()

        # Run IOR in Foreground
        self.run_enospace_foreground()

        # Stop running ior reads in the ior_bg_thread thread
        stop_ior_read.set()

        # Wait until the IOR Background thread completed
        job.join()

        # Verify the background job result has no FAIL for any IOR run
        for _result in self.test_result:
            if "FAIL" in _result:
                self.fail("One of the Background IOR job failed")

    def test_enospace_lazy_with_bg(self):
        """Jira ID: DAOS-4756.

        Test Description: IO gets DER_NOSPACE when SCM and NVMe is full with
                          default (lazy) Aggregation mode.

        Use Case: This tests will create the pool and fill 75% of SCM size which
                  will trigger the aggregation because of space pressure,
                  next fill 75% more which should fill NVMe. Try to fill 60%
                  more and now SCM size will be full too.
                  verify that last IO fails with DER_NOSPACE and SCM/NVMe pool
                  capacity is full.One background IO job will be running
                  continuously.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,der_enospace,enospc_lazy,enospc_lazy_bg
        :avocado: tags=NvmeEnospace,test_enospace_lazy_with_bg
        """
        self.log.info(self.pool.pool_percentage_used())

        # Run IOR to fill the pool.
        self.run_enospace_with_bg_job()
        self.log.info("Test passed")

    def test_enospace_lazy_with_fg(self):
        """Jira ID: DAOS-4756.

        Test Description: Fill up the system (default aggregation mode) and
                          delete all containers in loop, which should release
                          the space.

        Use Case: This tests will create the pool and fill 75% of SCM size which
                  will trigger the aggregation because of space pressure,
                  next fill 75% more which should fill NVMe. Try to fill 60%
                  more and now SCM size will be full too.
                  verify that last IO fails with DER_NOSPACE and SCM/NVMe pool
                  capacity is full. Delete all the containers.
                  Do this in loop for 10 times and verify space is released.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,der_enospace,enospc_lazy,enospc_lazy_fg
        :avocado: tags=NvmeEnospace,test_enospace_lazy_with_fg
        """
        self.log.info(self.pool.pool_percentage_used())

        # Repeat the test in loop.
        for _loop in range(10):
            self.log.info("-------enospc_lazy_fg Loop--------- %d", _loop)
            # Run IOR to fill the pool.
            self.run_enospace_foreground()
            # Delete all the containers
            self.delete_all_containers()
            # Delete container will take some time to release the space
            time.sleep(60)

        # Run last IO
        self.start_ior_load(storage='SCM', operation="Auto_Write", percent=1)

    def test_enospace_time_with_bg(self):
        """Jira ID: DAOS-4756.

        Test Description: IO gets DER_NOSPACE when SCM is full and it release
                          the size when container destroy with Aggregation
                          set on time mode.

        Use Case: This tests will create the pool. Set Aggregation mode to Time.
                  Start filling 75% of SCM size. Aggregation will be triggered
                  time to time, next fill 75% more which will fill up NVMe.
                  Try to fill 60% more and now SCM size will be full too.
                  Verify last IO fails with DER_NOSPACE and SCM/NVMe pool
                  capacity is full.One background IO job will be running
                  continuously.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,der_enospace,enospc_time,enospc_time_bg
        :avocado: tags=NvmeEnospace,test_enospace_time_with_bg
        """
        self.log.info(self.pool.pool_percentage_used())

        # Enabled TIme mode for Aggregation.
        self.pool.set_property("reclaim", "time")

        # Run IOR to fill the pool.
        self.run_enospace_with_bg_job()

    def test_enospace_time_with_fg(self):
        """Jira ID: DAOS-4756.

        Test Description: Fill up the system (time aggregation mode) and
                          delete all containers in loop, which should release
                          the space.

        Use Case: This tests will create the pool. Set Aggregation mode to Time.
                  Start filling 75% of SCM size. Aggregation will be triggered
                  time to time, next fill 75% more which will fill up NVMe.
                  Try to fill 60% more and now SCM size will be full too.
                  Verify last IO fails with DER_NOSPACE and SCM/NVMe pool
                  capacity is full. Delete all the containers.
                  Do this in loop for 10 times and verify space is released.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,der_enospace,enospc_time,enospc_time_fg
        :avocado: tags=NvmeEnospace,test_enospace_time_with_fg
        """
        self.log.info(self.pool.pool_percentage_used())

        # Enabled TIme mode for Aggregation.
        self.pool.set_property("reclaim", "time")

        # Repeat the test in loop.
        for _loop in range(10):
            self.log.info("-------enospc_time_fg Loop--------- %d", _loop)
            self.log.info(self.pool.pool_percentage_used())
            # Run IOR to fill the pool.
            self.run_enospace_with_bg_job()
            # Delete all the containers
            self.delete_all_containers()
            # Delete container will take some time to release the space
            time.sleep(60)

        # Run last IO
        self.start_ior_load(storage='SCM', operation="Auto_Write", percent=1)

    @skipForTicket("DAOS-8896")
    def test_performance_storage_full(self):
        """Jira ID: DAOS-4756.

        Test Description: Verify IO Read performance when pool size is full.

        Use Case: This tests will create the pool. Run small set of IOR as
                  baseline.Start IOR with < 4K which will start filling SCM
                  and trigger aggregation and start filling up NVMe.
                  Check the IOR baseline read number and make sure it's +- 5%
                  to the number ran prior system storage was full.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,der_enospace,enospc_performance
        :avocado: tags=NvmeEnospace,test_performance_storage_full
        """
        # Write the IOR Baseline and get the Read BW for later comparison.
        self.log.info(self.pool.pool_percentage_used())
        # Write First
        self.start_ior_load(storage='SCM', operation="Auto_Write", percent=1)
        # Read the baseline data set
        self.start_ior_load(storage='SCM', operation='Auto_Read', percent=1)
        max_mib_baseline = float(self.ior_matrix[0][int(IorMetrics.MAX_MIB)])
        baseline_cont_uuid = self.ior_cmd.dfs_cont.value
        self.log.info("IOR Baseline Read MiB %s", max_mib_baseline)

        # Run IOR to fill the pool.
        self.run_enospace_with_bg_job()

        # Read the same container which was written at the beginning.
        self.container.uuid = baseline_cont_uuid
        self.start_ior_load(storage='SCM', operation='Auto_Read', percent=1)
        max_mib_latest = float(self.ior_matrix[0][int(IorMetrics.MAX_MIB)])
        self.log.info("IOR Latest Read MiB %s", max_mib_latest)

        # Check if latest IOR read performance is in Tolerance of 5%, when
        # Storage space is full.
        if abs(max_mib_baseline - max_mib_latest) > (max_mib_baseline / 100 * 5):
            self.fail('Latest IOR read performance is not under 5% Tolerance'
                      ' Baseline Read MiB = {} and latest IOR Read MiB = {}'
                      .format(max_mib_baseline, max_mib_latest))

    def test_enospace_no_aggregation(self):
        """Jira ID: DAOS-4756.

        Test Description: IO gets DER_NOSPACE when SCM is full and it release
                          the size when container destroy with Aggregation
                          disabled.

        Use Case: This tests will create the pool and disable aggregation. Fill
                  75% of SCM size which should work, next try fill 10% more
                  which should fail with DER_NOSPACE. Destroy the container
                  and validate the Pool SCM free size is close to full (> 95%).
                  Do this in loop ~10 times and verify the DER_NOSPACE and SCM
                  free size after container destroy.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,der_enospace,enospc_no_aggregation
        :avocado: tags=NvmeEnospace,test_enospace_no_aggregation
        """
        # pylint: disable=attribute-defined-outside-init
        # pylint: disable=too-many-branches
        self.log.info(self.pool.pool_percentage_used())

        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")

        # Get the DER_NOSPACE and other error count from log
        self.der_nospace_count, self.other_errors_count = error_count(
            "-1007", self.hostlist_clients, self.client_log)

        # Repeat the test in loop.
        for _loop in range(10):
            self.log.info("-------enospc_no_aggregation Loop--------- %d", _loop)
            # Fill 75% of SCM pool
            self.start_ior_load(storage='SCM', operation="Auto_Write", percent=40)

            self.log.info(self.pool.pool_percentage_used())

            try:
                # Fill 10% more to SCM ,which should Fail because no SCM space
                self.start_ior_load(storage='SCM', operation="Auto_Write", percent=40)
                self.fail('This test suppose to fail because of DER_NOSPACE'
                          'but it got Passed')
            except TestFail:
                self.log.info('Expected to fail because of DER_NOSPACE')

            # Verify DER_NO_SAPCE error count is expected and no other Error in client log.
            self.verify_enospace_log(self.der_nospace_count)

            # Delete all the containers
            self.delete_all_containers()

            # Wait for the SCM space to be released. (Usage goes below 60%)
            scm_released = False
            pool_usage = None
            for count in range(6):
                time.sleep(10)
                pool_usage = self.pool.pool_percentage_used()
                self.log.info("Pool usage at iter %d: %s", count, pool_usage)
                if pool_usage["scm"] < 60:
                    scm_released = True
                    break

            # Verify that the SCM usage has gone down below 60%.
            if not scm_released:
                msg = (f"Pool SCM used percentage should be < 60%. Actual = "
                       f"{pool_usage['scm']}")
                self.fail(msg)

        # Run last IO
        self.start_ior_load(storage='SCM', operation="Auto_Write", percent=1)
