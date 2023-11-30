'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import threading
import time

from apricot import skipForTicket
from avocado.core.exceptions import TestFail
from daos_utils import DaosCommand
from exception_utils import CommandFailure
from general_utils import get_display_size, get_errors_count
from ior_utils import IorCommand, IorMetrics
from job_manager_utils import get_job_manager
from nvme_utils import ServerFillUp
from pydaos.raw import c_err_to_str
from telemetry_test_base import TestWithTelemetry


class NvmeEnospace(ServerFillUp, TestWithTelemetry):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate DER_NOSPACE for SCM and NVMe
    :avocado: recursive
    """

    DER_NOSPACE = -1007
    DER_TIMEDOUT = -1011

    def __init__(self, *args, **kwargs):
        """Initialize a NvmeEnospace object."""
        super().__init__(*args, **kwargs)

        self.metric_names = ['engine_pool_vos_space_scm_used', 'engine_pool_vos_space_nvme_used']
        self.media_names = ['SCM', 'NVMe']
        self.expected_errors = [self.DER_NOSPACE, self.DER_TIMEDOUT]

        self.pool_usage_min = []
        self.test_result = []
        self.daos_cmd = None

    def setUp(self):
        """Initial setup."""
        super().setUp()

        self.test_result = []

        for elt in self.media_names:
            self.pool_usage_min.append(float(
                self.params.get(elt.casefold(), "/run/pool/usage_min/*", 0)))

        # initialize daos command
        self.daos_cmd = DaosCommand(self.bin)
        self.create_pool_max_size()

    def get_pool_space_metrics(self, pool_uuid):
        """Return the metrics on space usage of a given pool.

        Args:
            pool_uuid (str): Unique id of a pool.

        Returns:
            dict: metrics on space usage.

        """
        metrics = {}
        for hostname, data in self.telemetry.get_metrics(",".join(self.metric_names)).items():
            for metric_name, entry in data.items():
                if metric_name not in metrics:
                    metrics[metric_name] = {
                        "description": entry['description'],
                        "hosts": {}
                    }

                hosts = metrics[metric_name]["hosts"]
                for metric in entry['metrics']:
                    if metric['labels']['pool'].casefold() != pool_uuid.casefold():
                        continue

                    if hostname not in hosts:
                        hosts[hostname] = {}

                    rank = metric['labels']['rank']
                    if rank not in hosts[hostname]:
                        hosts[hostname][rank] = {}

                    target = metric['labels']['target']
                    hosts[hostname][rank][target] = metric['value']

        return metrics

    def get_pool_usage(self, pool_space):
        """Get the pool storage used % for SCM and NVMe.

        Returns:
            list: a list of SCM/NVMe pool space usage in %(float)

        """
        pool_usage = []
        for idx, _ in enumerate(self.media_names):
            s_total = pool_space.ps_space.s_total[idx]
            s_free = pool_space.ps_space.s_free[idx]
            pool_usage.append((100 * (s_total - s_free)) / s_total)

        return pool_usage

    def display_pool_stats(self, pool_space, pool_space_metrics):
        """Display statistics on pool usage.

        Args:
            pool_space (object): space usage information of a pool.
            pool_space_metrics (dict): dict of metrics on space usage of a pool.
        """

        title = f"{' Pool Space Usage ':-^80}"
        self.log.debug(title)

        pool_usage = self.get_pool_usage(pool_space)
        for idx, elt in enumerate(self.media_names):
            self.log.debug("%s Space Stat:", elt)
            self.log.debug(
                "\t- Total used space:          %.2f%%",
                pool_usage[idx])
            self.log.debug(
                "\t- Average target free space: %s",
                get_display_size(pool_space.ps_free_mean[idx]))
            self.log.debug(
                "\t- Minimal target free space: %s",
                get_display_size(pool_space.ps_free_min[idx]))
            self.log.debug(
                "\t- Maximal target free space: %s",
                get_display_size(pool_space.ps_free_max[idx]))

        for metric in pool_space_metrics.values():
            table = [["Hostname", "Rank", "Target", "Size"]]
            cols_size = []
            for cell in table[0]:
                cols_size.append(len(cell))
            for hostname, ranks in metric['hosts'].items():
                for rank, targets in ranks.items():
                    for target, size in targets.items():
                        row = [hostname, rank, target, get_display_size(size)]
                        table.append(row)
                        for idx, elt in enumerate(cols_size):
                            cols_size[idx] = max(elt, len(row[idx]))
                        hostname = ""
                        rank = ""

            for idx, elt in enumerate(table[0]):
                table[0][idx] = f"{elt:^{cols_size[idx]}}"
            row = ' | '.join(table[0])
            title = f"{' ' + metric['description'] + ' ':-^{len(row)}}"
            self.log.debug("")
            self.log.debug(title)
            self.log.debug(row)
            self.log.debug("-" * len(row))
            for row in table[1:]:
                for idx, elt in enumerate(row):
                    align_op = "<"
                    if idx + 1 == len(row):
                        align_op = ">"
                    row[idx] = f"{elt:{align_op}{cols_size[idx]}}"
                self.log.debug(" | ".join(row))

    def verify_enospace_log(self, log_file):
        """Function checking logs consistency.

        Function checking that only expected errors have occurred and the DER_NOSPACE errors have
        occurred.

        Args:
            log_file (str): name prefix of the log files to check.
        """
        logfile_glob = log_file + r".*[0-9]"
        errors_count = get_errors_count(self.hostlist_clients, logfile_glob)
        for error in self.expected_errors:
            if error not in errors_count:
                errors_count[error] = 0

        if errors_count[self.DER_NOSPACE] <= 0:
            self.fail(
                "Expected DER_NOSPACE (-1007) should be > 0: got={}"
                .format(errors_count[self.DER_NOSPACE]))

        if len(self.expected_errors) != len(errors_count):
            unexpected_errors_count = {
                key: val for key, val in errors_count.items() if key not in self.expected_errors
            }
            msg = 'Found unexpected errors in client logs {}: '.format(logfile_glob)
            msg += ", ".join(
                f'{c_err_to_str(key)}({key}): got={val}'
                for key, val in unexpected_errors_count.items())
            self.fail(msg)

        for error in self.expected_errors:
            if error == self.DER_NOSPACE:
                continue
            if errors_count[error] == 0:
                continue
            self.log.info(
                "Number of errors %s (%s) is > 0: got=%d",
                c_err_to_str(error), error, errors_count[error])

    def delete_all_containers(self):
        """Delete all the containers."""
        # List all the container
        kwargs = {"pool": self.pool.uuid}
        data = self.daos_cmd.container_list(**kwargs)
        containers = [uuid_label["uuid"] for uuid_label in data["response"]]

        # Destroy all the containers
        for _cont in containers:
            kwargs["cont"] = _cont
            kwargs["force"] = True
            self.daos_cmd.container_destroy(**kwargs)

    def verify_background_job(self):
        """Function to verify that no background jobs have failed during the test."""
        for _result in self.test_result:
            if "FAIL" in _result:
                self.fail("One of the Background IOR job failed")

    def ior_bg_thread(self, event):
        """Start IOR Background thread.

        This will write small data set and keep reading it in loop until it fails or main program
        exit.

        Args:
            event(obj): Event indicator to stop IOR read.
        """
        self.log.info('----Starting background IOR load----')

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
        self.log.info('--Run IOR Write in Background--')
        # run IOR Write Command
        try:
            job_manager.run()
        except (CommandFailure, TestFail) as exc:
            self.log.info("Background ior write failed: %s", str(exc))
            self.test_result.append("FAIL - ior write")
            return

        # run IOR Read Command in loop
        self.log.info('--Run IOR Read in Background--')
        ior_bg_cmd.flags.update(self.ior_read_flags)
        stop_looping = False
        while not stop_looping:
            try:
                job_manager.run()
            except (CommandFailure, TestFail) as exc:
                self.log.info("Background ior read failed: %s", str(exc))
                self.test_result.append("FAIL - ior read")
                break
            stop_looping = event.wait(1)

    def run_enospace_foreground(self, log_file):
        """Fill SCM and NVMe devices.

        Run IOR to fill up SCM and NVMe. Verify that we see DER_NOSPACE while filling up SCM. Then
        verify that the storage usage is near 100%.

        Args:
            log_file (str): name prefix of the log files to check.
        """
        self.log.info('----Starting main IOR load----')

        # Fill 75% of current SCM free space. Aggregation is Enabled so NVMe space will
        # start to fill up.
        self.log.info('--Filling 75% of the current SCM free space--')
        self.start_ior_load(storage='SCM', operation="Auto_Write", percent=75)
        self.log.info(self.pool.pool_percentage_used())

        # Fill 50% of current SCM free space. Aggregation is Enabled so NVMe space will
        # continue to fill up.
        self.start_ior_load(storage='SCM', operation="Auto_Write", percent=50)
        self.log.info(self.pool.pool_percentage_used())

        # Fill 60% of current SCM free space. This time, NVMe will be Full so data will
        # not be moved to NVMe and continue to fill up SCM. SCM will be full and this
        # command is expected to fail with DER_NOSPACE.
        self.log.info('--Filling 60% of the current SCM free space--')
        try:
            self.start_ior_load(
                storage='SCM', operation="Auto_Write", percent=60, log_file=log_file)
        except TestFail:
            self.log.info('Test is expected to fail because of DER_NOSPACE')
        else:
            self.fail('This test is suppose to FAIL because of DER_NOSPACE'
                      'but it Passed')

        # Display the pool statistics
        self.pool.get_info()
        pool_space = self.pool.info.pi_space
        pool_space_metrics = self.get_pool_space_metrics(self.pool.uuid)
        self.display_pool_stats(pool_space, pool_space_metrics)

        # verify the DER_NO_SPACE error count is expected and no other Error in client log
        self.verify_enospace_log(log_file)

        # Check both NVMe and SCM are full.
        pool_usage = self.get_pool_usage(pool_space)
        for idx, elt in enumerate(self.media_names):
            if pool_usage[idx] >= self.pool_usage_min[idx]:
                continue
            msg = (
                f"Pool {elt} used percentage is invalid: "
                f"wait_in=[{self.pool_usage_min[idx]}, 100], got={pool_usage[idx]}"
            )
            self.fail(msg)

    def run_enospace_with_bg_job(self, log_file):
        """Check DER_ENOSPACE occurs when storage space is filled.

        Stress test to validate DER_ENOSPACE management and expected storage size. Single IOR job
        will run in background while space is filling.

        Args:
            log_file (str): name prefix of the log files to check.
        """
        # Start the IOR Background thread which will write small data set and
        # read in loop, until storage space is full.
        job = threading.Thread(target=self.ior_bg_thread)
        stop_ior_read = threading.Event()
        job = threading.Thread(target=self.ior_bg_thread, args=[stop_ior_read])
        job.daemon = True
        job.start()

        # Run IOR in Foreground
        self.run_enospace_foreground(log_file)

        # Stop running ior reads in the ior_bg_thread thread
        stop_ior_read.set()

        # Wait until the IOR Background thread completed
        job.join()

        # Verify the background job result has no FAIL for any IOR run
        self.verify_background_job()

    def test_enospace_lazy_with_bg(self):
        """Jira ID: DAOS-4756.

        Test Description: IO gets DER_NOSPACE when SCM and NVMe is full with default (lazy)
                          Aggregation mode.

        Use Case: This tests will create the pool and fill 75% of SCM size which will trigger the
                  aggregation because of space pressure, next fill 75% more which should fill NVMe.
                  Try to fill 60% more and now SCM size will be full too.  verify that last IO fails
                  with DER_NOSPACE and SCM/NVMe pool capacity is full.One background IO job will be
                  running continuously.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,der_enospace,enospc_lazy,enospc_lazy_bg
        :avocado: tags=NvmeEnospace,test_enospace_lazy_with_bg
        """
        self.log.info(self.pool.pool_percentage_used())

        # Run IOR to fill the pool.
        self.run_enospace_with_bg_job(self.client_log)
        self.log.info("Test passed")

    def test_enospace_lazy_with_fg(self):
        """Jira ID: DAOS-4756.

        Test Description: Fill up the system (default aggregation mode) and delete all containers in
                          loop, which should release the space.

        Use Case: This tests will create the pool and fill 75% of SCM size which will trigger the
                  aggregation because of space pressure, next fill 75% more which should fill NVMe.
                  Try to fill 60% more and now SCM size will be full too.  verify that last IO fails
                  with DER_NOSPACE and SCM/NVMe pool capacity is full. Delete all the containers.
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
            log_file = f"-loop_{_loop}".join(os.path.splitext(self.client_log))
            self.run_enospace_foreground(log_file)
            # Delete all the containers
            self.delete_all_containers()
            # Delete container will take some time to release the space
            time.sleep(60)

        # Run last IO
        self.start_ior_load(storage='SCM', operation="Auto_Write", percent=1)

    def test_enospace_time_with_bg(self):
        """Jira ID: DAOS-4756.

        Test Description: IO gets DER_NOSPACE when SCM is full and it release the size when
                          container destroy with Aggregation set on time mode.

        Use Case: This tests will create the pool. Set Aggregation mode to Time.  Start filling 75%
                  of SCM size. Aggregation will be triggered time to time, next fill 75% more which
                  will fill up NVMe.  Try to fill 60% more and now SCM size will be full too.
                  Verify last IO fails with DER_NOSPACE and SCM/NVMe pool capacity is full.One
                  background IO job will be running continuously.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,der_enospace,enospc_time,enospc_time_bg
        :avocado: tags=NvmeEnospace,test_enospace_time_with_bg
        """
        self.log.info(self.pool.pool_percentage_used())

        # Enabled TIme mode for Aggregation.
        self.pool.set_property("reclaim", "time")

        # Run IOR to fill the pool.
        self.run_enospace_with_bg_job(self.client_log)

    def test_enospace_time_with_fg(self):
        """Jira ID: DAOS-4756.

        Test Description: Fill up the system (time aggregation mode) and delete all containers in
                          loop, which should release the space.

        Use Case: This tests will create the pool. Set Aggregation mode to Time.  Start filling 75%
                  of SCM size. Aggregation will be triggered time to time, next fill 75% more which
                  will fill up NVMe.  Try to fill 60% more and now SCM size will be full too.
                  Verify last IO fails with DER_NOSPACE and SCM/NVMe pool capacity is full. Delete
                  all the containers.  Do this in loop for 10 times and verify space is released.

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
            log_file = f"-loop_{_loop}".join(os.path.splitext(self.client_log))
            self.run_enospace_with_bg_job(log_file)
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

        Use Case: This tests will create the pool. Run small set of IOR as baseline.Start IOR with
                  < 4K which will start filling SCM and trigger aggregation and start filling up
                  NVMe.  Check the IOR baseline read number and make sure it's +- 5% to the number
                  ran prior system storage was full.

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
        self.run_enospace_with_bg_job(self.client_log)

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

        Test Description: IO gets DER_NOSPACE when SCM is full and it release the size when
                          container destroy with Aggregation disabled.

        Use Case: This tests will create the pool and disable aggregation. Fill 75% of SCM size
                  which should work, next try fill 10% more which should fail with DER_NOSPACE.
                  Destroy the container and validate the Pool SCM free size is close to full (>
                  95%).  Do this in loop ~10 times and verify the DER_NOSPACE and SCM free size
                  after container destroy.

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

        # Repeat the test in loop.
        for _loop in range(10):
            self.log.info("-------enospc_no_aggregation Loop--------- %d", _loop)
            # Fill 75% of SCM pool
            self.start_ior_load(storage='SCM', operation="Auto_Write", percent=40)

            self.log.info(self.pool.pool_percentage_used())

            log_file = f"-loop_{_loop}".join(os.path.splitext(self.client_log))
            try:
                # Fill 10% more to SCM ,which should Fail because no SCM space
                self.start_ior_load(
                    storage='SCM', operation="Auto_Write", percent=40, log_file=log_file)
            except TestFail:
                self.log.info('Expected to fail because of DER_NOSPACE')
            else:
                self.fail('This test suppose to fail because of DER_NOSPACE'
                          'but it got Passed')

            # Verify DER_NO_SPACE error count is expected and no other Error in client log.
            self.verify_enospace_log(log_file)

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
