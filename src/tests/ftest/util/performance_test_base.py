"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time

import oclass_utils
from avocado.core.exceptions import TestFail
from exception_utils import CommandFailure
from ior_test_base import IorTestBase
from ior_utils import IorMetrics
from mdtest_test_base import MdtestBase
from mdtest_utils import MdtestMetrics


class PerformanceTestBase(IorTestBase, MdtestBase):
    """Base performance class.

    Optional yaml config values:
        performance/phase_barrier_s (int): seconds to wait between IOR write/read phases.
        performance/env (list): list of env vars to set for IOR/MDTest.

    Outputs:
        */data/performance.log: Contains input parameters and output metrics.
        */data/daos_metrics/<host>_engine<idx>.csv: daos_metrics output for each host/engine
    """

    class PerfParams():
        # pylint: disable=too-few-public-methods
        """Data class for performance params"""

        def __init__(self):
            """Init performance params"""
            self.num_servers = 0
            self.num_engines = 0
            self.num_targets = 0
            self.num_clients = 0
            self.ppn = 0
            self.provider = None

    def __init__(self, *args, **kwargs):
        """Initialize a PerformanceTestBase object."""
        super().__init__(*args, **kwargs)

        # Need to restart so daos_metrics and system state is fresh
        self.start_servers_once = False

        self.dmg_cmd = None
        self.daos_cmd = None
        self._performance_log_dir = self.outputdir
        self._performance_log_name = os.path.join(self._performance_log_dir, "performance.log")
        self.phase_barrier_s = 0
        self.daos_metrics_num = 0

        # For tracking various configuration params
        self.perf_params = PerformanceTestBase.PerfParams()

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        self.dmg_cmd = self.get_dmg_command()
        self.daos_cmd = self.get_daos_command()
        engines_per_host = self.server_managers[0].get_config_value("engines_per_host") or 1
        self.perf_params.num_servers = len(self.hostlist_servers)
        self.perf_params.num_engines = engines_per_host * self.perf_params.num_servers
        self.perf_params.num_targets = int(self.server_managers[0].get_config_value("targets"))
        self.perf_params.num_clients = len(self.hostlist_clients)
        self.perf_params.provider = self.server_managers[0].get_config_value("provider")
        self.phase_barrier_s = self.params.get("phase_barrier_s", '/run/performance/*', 0)

    def log_performance(self, msg, log_to_info=True, file_path=None):
        """Log a performance-related message to self.log.info and self._performance_log_name.

        file_path is opened/closed each time; thus, should be called infrequently.

        Args:
            msg (str/list): message or list of messages to log.
            log_to_info (bool, optional): whether to log to self.log.info.
                Defaults to True.
            file_path (str, optional): file to append to.
                Defaults to self._performance_log_name.

        Args:
            msg (str): the message.

        """
        if not isinstance(msg, (list, tuple)):
            msg = [msg]
        file_path = file_path or self._performance_log_name

        # Log each message individually to self.log
        if log_to_info:
            for _msg in msg:
                self.log.info(_msg)

        # Log a single combined message to file_path
        combined_msg = "\n".join(msg)
        with open(file_path, "a") as log:
            log.write(combined_msg + "\n")

    def _log_daos_metrics(self):
        """Get and log the daos_metrics for each server."""
        self.daos_metrics_num += 1
        metrics_dir = os.path.join(self._performance_log_dir, "daos_metrics")
        if self.daos_metrics_num > 1:
            metrics_dir += str(self.daos_metrics_num)
        os.makedirs(metrics_dir, exist_ok=True)
        per_engine_results = self.server_managers[0].get_daos_metrics()
        for engine_idx, engine_results in enumerate(per_engine_results):
            for hosts, stdout in engine_results.all_stdout.items():
                log_name = "{}_engine{}.csv".format(hosts, engine_idx)
                log_path = os.path.join(metrics_dir, log_name)
                self.log_performance(stdout, False, log_path)

    @property
    def unique_id(self):
        """A unique id for each test case ran."""
        return "{}-{}".format(self.job_id, os.getpid())

    def phase_barrier(self):
        """Sleep barrier meant to be used between IO phases.

        Useful for flushing system IO.
        """
        if self.phase_barrier_s > 0:
            self.log.info("Sleeping for %s seconds", str(self.phase_barrier_s))
            time.sleep(self.phase_barrier_s)

    def _log_performance_params(self, group, extra_params=None):
        """Log performance parameters.

        Args:
            group (str): IOR or MDTEST
            extra_params (list, optional): extra params to print. Default is None.

        """
        if group not in ("IOR", "MDTEST"):
            self.fail("Invalid group: {}".format(group))

        # Start with common parameters
        # Build a list of [PARAM_NAME, PARAM_VALUE]
        params = [
            ["TEST_ID", self.unique_id],
            ["TEST_NAME", self.test_id],
            ["TEST_GROUP", group],
            ["NUM_SERVERS", self.perf_params.num_servers],
            ["NUM_ENGINES", self.perf_params.num_engines],
            ["NUM_TARGETS", self.perf_params.num_targets],
            ["NUM_CLIENTS", self.perf_params.num_clients],
            ["PPN", self.perf_params.ppn],
            ["PROVIDER", self.perf_params.provider]
        ]

        # Get group-specific parameters
        if group == "IOR":
            params += [
                ["API", self.ior_cmd.api.value],
                ["OCLASS", self.ior_cmd.dfs_oclass.value],
                ["XFER_SIZE", self.ior_cmd.transfer_size.value],
                ["BLOCK_SIZE", self.ior_cmd.block_size.value],
                ["SW_TIME", self.ior_cmd.sw_deadline.value],
                ["CHUNK_SIZE", self.ior_cmd.dfs_chunk.value],
                ["SEGMENTS", self.ior_cmd.segment_count.value],
                ["ITERATIONS", self.ior_cmd.repetitions.value]
            ]
        elif group == "MDTEST":
            params += [
                ["API", self.mdtest_cmd.api.value],
                ["OCLASS", self.mdtest_cmd.dfs_oclass.value],
                ["DIR_OCLASS", self.mdtest_cmd.dfs_dir_oclass.value],
                ["SW_TIME", self.mdtest_cmd.stonewall_timer.value],
                ["CHUNK_SIZE", self.mdtest_cmd.dfs_chunk.value],
                ["N_FILE", self.mdtest_cmd.num_of_files_dirs.value],
                ["BYTES_READ", self.mdtest_cmd.read_bytes.value],
                ["BYTES_WRITE", self.mdtest_cmd.write_bytes.value],
                ["MDTEST_FLAGS", self.mdtest_cmd.flags.value]
            ]

        # Add the extra params
        if extra_params is not None:
            params += extra_params

        # Print and align all parameters in the format:
        # PARAM_NAME : PARAM_VALUE
        max_len = max((len(param[0]) for param in params))
        self.log_performance(
            ["{:<{}} : {}".format(param[0], max_len, param[1]) for param in params]
        )

    def set_processes_ppn(self, namespace):
        """Set processes and ppn from a given namespace.

        Args:
            namespace (str): config namespace. E.g. /run/ior/*

        """
        self.processes = self.params.get("np", namespace, self.processes)
        self.ppn = self.perf_params.ppn = self.params.get("ppn", namespace, self.ppn)

    def verify_system_status(self, pool=None, container=None):
        """Verify system/pool/container status (in that order).

        Args:
            pool (TestPool, optional): Pool to check status of. Default is None.
            container (TestContainer, optional): Container to check status of. Default is None.

        Raises:
            CommandFailure, TestFail: if there is an error checking system status

        """
        funcs = [self.dmg_cmd.system_query]
        if pool:
            funcs.append(pool.set_query_data)
        if container:
            funcs.append(container.query)

        first_error = None
        for func in funcs:
            try:
                func()
            except (CommandFailure, TestFail) as error:
                self.log.error(error)
                first_error = first_error or error

        if first_error:
            raise first_error

    def verify_oclass_engine_count(self, oclass, fail=True):
        """Verify an object class is compatible with the number of engines.

        Args:
            oclass (str): The object class. Assumed to be valid.
            fail (bool): Whether to fail the test when there aren't enough engines.
                Defaults to True.

        Returns:
            bool: True if there are enough engines. False otherwise.

        """
        min_engines = oclass_utils.calculate_min_engines(oclass)
        if self.perf_params.num_engines < min_engines:
            msg = "Need at least {} engines for oclass {}".format(min_engines, oclass)
            if fail:
                self.fail(msg)
            else:
                self.log.info(msg)
            return False
        return True

    def _run_performance_ior_single(self, intercept=None):
        """Run a single IOR execution.

        Args:
            intercept (str, optional): path to interception library.

        """
        try:
            ior_output = self.run_ior_with_pool(
                create_pool=False,
                create_cont=False,
                intercept=intercept,
                display_space=False,
                stop_dfuse=False
            )
            ior_metrics = self.ior_cmd.get_ior_metrics(ior_output)
            for metrics in ior_metrics:
                if metrics[0] == "write":
                    self.log_performance("Max Write: {}".format(metrics[IorMetrics.MAX_MIB]))
                elif metrics[0] == "read":
                    self.log_performance("Max Read: {}".format(metrics[IorMetrics.MAX_MIB]))
        except (CommandFailure, TestFail):
            try:
                self._log_daos_metrics()
            except (CommandFailure, TestFail):
                pass
            raise
        finally:
            # Try this even if IOR failed because it could give us useful info
            self.verify_system_status(self.pool, self.container)

    def run_performance_ior(self, namespace=None):
        """Run an IOR performance test.

        Write and Read are ran separately.

        Args:
            namespace (str, optional): namespace for IOR parameters in the yaml.
                Defaults to None, which uses default IOR namespace.

        """
        if namespace is not None:
            self.ior_cmd.namespace = namespace
            self.ior_cmd.get_params(self)
            self.set_processes_ppn(self.ior_cmd.namespace)

        if self.ior_cmd.api.value == 'POSIX+IOIL':
            intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
            self.ior_cmd.api.update('POSIX')
        elif self.ior_cmd.api.value == 'POSIX+PIL4DFS':
            intercept = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
            self.ior_cmd.api.update('POSIX')
        else:
            intercept = None

        # Save write and read params for switching
        write_flags = self.params.get("write_flags", self.ior_cmd.namespace)
        read_flags = self.params.get("read_flags", self.ior_cmd.namespace)
        if write_flags is None:
            self.fail("write_flags not found in config")
        if read_flags is None:
            self.fail("read_flags not found in config")

        self._log_performance_params(
            "IOR",
            [["IOR Write Flags", write_flags],
             ["IOR Read Flags", read_flags]])

        self.verify_oclass_engine_count(self.ior_cmd.dfs_oclass.value)

        # Set the container redundancy factor to match the oclass
        cont_rf = oclass_utils.extract_redundancy_factor(self.ior_cmd.dfs_oclass.value)

        # Create pool and container upfront for flexibility
        self.pool = self.get_pool(connect=False)
        params = {}
        if self.ior_cmd.dfs_oclass.value:
            params['oclass'] = self.ior_cmd.dfs_oclass.value
        if self.ior_cmd.dfs_chunk.value:
            params['chunk_size'] = self.ior_cmd.dfs_chunk.value
        self.container = self.get_container(self.pool, create=False, **params)
        rf_prop = "rd_fac:{}".format(cont_rf)
        current_properties = self.container.properties.value
        new_properties = ','.join(filter(None, (current_properties, rf_prop)))
        self.container.properties.update(new_properties)
        self.container.create()
        self.update_ior_cmd_with_pool(False)

        self.log_step("Running IOR write")
        self.ior_cmd.flags.update(write_flags)
        self._run_performance_ior_single(intercept)

        # Manually stop dfuse after ior write completes
        if self.dfuse:
            self.dfuse.stop()
            self.dfuse = None

        # Wait between write and read
        self.phase_barrier()

        self.log_step("Running IOR read")
        self.ior_cmd.flags.update(read_flags)
        self._run_performance_ior_single(intercept)

        # Manually stop dfuse after ior read completes
        if self.dfuse:
            self.dfuse.stop()
            self.dfuse = None

        self._log_daos_metrics()

    def run_performance_mdtest(self, namespace=None):
        """Run an MDTest performance test.

        Args:
            namespace (str, optional): namespace for MDTest parameters in the yaml.
                Defaults to None, which uses default MDTest namespace.

        """
        if namespace is not None:
            self.mdtest_cmd.namespace = namespace
            self.mdtest_cmd.get_params(self)
            self.set_processes_ppn(self.mdtest_cmd.namespace)

        if self.mdtest_cmd.api.value == 'POSIX+IOIL':
            self.mdtest_cmd.env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libioil.so')
            self.mdtest_cmd.api.update('POSIX')
        elif self.mdtest_cmd.api.value == 'POSIX+PIL4DFS':
            self.mdtest_cmd.env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
            self.mdtest_cmd.api.update('POSIX')

        self._log_performance_params("MDTEST")

        self.verify_oclass_engine_count(self.mdtest_cmd.dfs_oclass.value)
        self.verify_oclass_engine_count(self.mdtest_cmd.dfs_dir_oclass.value)

        # Track which phases are being ran. If not individually specified, assume all
        phase_create = '-C' in self.mdtest_cmd.flags.value
        phase_stat = '-T' in self.mdtest_cmd.flags.value
        phase_read = '-E' in self.mdtest_cmd.flags.value
        phase_remove = '-r' in self.mdtest_cmd.flags.value
        if not any((phase_create, phase_stat, phase_read, phase_remove)):
            phase_create = phase_stat = phase_read = phase_remove = True

        # Set the container redundancy factor to match the oclass used
        cont_rf = min([
            oclass_utils.extract_redundancy_factor(self.mdtest_cmd.dfs_oclass.value),
            oclass_utils.extract_redundancy_factor(self.mdtest_cmd.dfs_dir_oclass.value)])

        # Create pool and container upfront so rank stop timing is more accurate
        self.pool = self.get_pool(connect=False)
        params = {}
        if self.mdtest_cmd.dfs_oclass.value:
            params['oclass'] = self.mdtest_cmd.dfs_oclass.value
        if self.mdtest_cmd.dfs_dir_oclass.value:
            params['dir_oclass'] = self.mdtest_cmd.dfs_dir_oclass.value
        if self.mdtest_cmd.dfs_chunk.value:
            params['chunk_size'] = self.mdtest_cmd.dfs_chunk.value
        self.container = self.get_container(self.pool, create=False, **params)
        rf_prop = "rd_fac:{}".format(cont_rf)
        current_properties = self.container.properties.value
        new_properties = ','.join(filter(None, (current_properties, rf_prop)))
        self.container.properties.update(new_properties)
        self.container.create()

        # Never let execute_mdtest automatically destroy the container
        self.mdtest_cmd.dfs_destroy.update(False)

        self.log.info("Running MDTEST")
        try:
            mdtest_result = self.execute_mdtest(display_space=False)
            mdtest_metrics = MdtestMetrics(mdtest_result.stdout_text)
            if not mdtest_metrics:
                self.fail("Failed to get mdtest metrics")
            log_list = []
            if mdtest_metrics.rates.file_creation.max > 0:
                log_list.append('create_ops: {}'.format(mdtest_metrics.rates.file_creation.max))
            if mdtest_metrics.rates.file_stat.max > 0:
                log_list.append('stat_ops: {}'.format(mdtest_metrics.rates.file_stat.max))
            if mdtest_metrics.rates.file_read.max > 0:
                log_list.append('read_ops: {}'.format(mdtest_metrics.rates.file_read.max))
            if mdtest_metrics.rates.file_removal.max > 0:
                log_list.append('remove_ops: {}'.format(mdtest_metrics.rates.file_removal.max))
            self.log_performance(log_list)
        except (CommandFailure, TestFail):
            try:
                self._log_daos_metrics()
            except (CommandFailure, TestFail):
                pass
            raise
        finally:
            # Try this even if MDTest failed because it could give us useful info
            self.verify_system_status(self.pool, self.container)

        # Manually stop dfuse after mdtest completes
        if self.dfuse:
            self.dfuse.stop()
            self.dfuse = None

        self._log_daos_metrics()
