#!/usr/bin/env python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time
import types

from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from mdtest_utils import MdtestMetrics
from general_utils import get_subprocess_stdout
from ior_utils import IorMetrics
from command_utils_base import EnvironmentVariables
import oclass_utils

# TODO dmg system query as non-json to reduce log clutter # pylint: disable=fixme

class PerformanceTestBase(IorTestBase, MdtestBase):
    # pylint: disable=too-many-ancestors
    """Base performance class.

    Optional yaml config values:
        performance/phase_barrier_s (int): seconds to wait between IOR write/read phases.
        performance/env (list): list of env vars to set for IOR/MDTest.

    Outputs:
        */data/performance.log: Contains input parameters and output metrics.
        */data/daos_metrics/<host>_engine<idx>.csv: daos_metrics output for each host/engine
    """

    def __init__(self, *args, **kwargs):
        """Initialize a PerformanceTestBase object."""
        super().__init__(*args, **kwargs)

        # Need to restart so daos_metrics and system state is fresh
        self.start_servers_once = False

        self.dmg_cmd = None
        self.daos_cmd = None
        self._performance_log_dir = self.outputdir
        self._performance_log_name = os.path.join(self._performance_log_dir, "performance.log")
        self.engines_per_host = 0
        self.num_servers = 0
        self.num_targets = 0
        self.num_clients = 0
        self.phase_barrier_s = 0
        self.performance_env = EnvironmentVariables()

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        self.dmg_cmd = self.get_dmg_command()
        self.daos_cmd = self.get_daos_command()
        self.engines_per_host = self.server_managers[0].get_config_value("engines_per_host") or 1
        self.num_servers = self.engines_per_host * len(self.hostlist_servers)
        self.num_targets = int(self.server_managers[0].get_config_value("targets"))
        self.num_clients = len(self.hostlist_clients)
        self.phase_barrier_s = self.params.get("phase_barrier_s", '/run/performance/*', 0)
        self.__parse_performance_env()
        self.__override_cmd_env(self.ior_cmd)
        self.__override_cmd_env(self.mdtest_cmd)

    def __parse_performance_env(self):
        """Parse the performance environment variables.

        This will be handled more cleanly by DAOS-9221. DO NOT COPY THIS.

        For example:
        performance:
            env:
                - D_LOG_MASK=ERR

        """
        perf_env = self.params.get("env", "/run/performance/*", {})
        if not perf_env:
            return
        for key_val in perf_env:
            key, val = key_val.split('=')
            self.performance_env[key] = val

    def __override_cmd_env(self, cmd):
        """Override get_default_env for a cmd to include the performance environment.

        Adds self.performance_env to self.{cmd}.get_default_env().
        This will be handled more cleanly by DAOS-9221. DO NOT COPY THIS.

        Args:
            cmd (Object): object containing the get_default_env function.
                Usually self.ior_cmd or self.mdtest_cmd.

        """
        old_get_default_env = cmd.get_default_env
        performance_env = self.performance_env

        def new_get_default_env(self, *args, **kwargs): # pylint: disable=unused-argument
            env = old_get_default_env(*args, **kwargs)
            for key, val in performance_env.items():
                env[key] = val
            return env
        setattr(cmd, "get_default_env", types.MethodType(new_get_default_env, cmd))

    def log_performance(self, msg, log_to_info=True, file_path=None):
        """Log a performance-related message to self.log.info and self._performance_log_name.

        Args:
            msg (str/list): message or list of messages to log.
            log_to_info (bool, optional): whether to log to self.log.info.
                Defaults to True.
            file_path (str, optional): file to append to.
                Defaults to self._performance_log_name.

        self._performance_log_name is opened/closed each time; thus, should be called infrequently.

        Args:
            msg (str): the message.

        """
        if not isinstance(msg, (list, tuple)):
            msg = [msg]
        if file_path is None:
            file_path = self._performance_log_name

        # Log each message individually to self.log
        if log_to_info:
            for m in msg:
                self.log.info(m)

        # Log a single combined message to file_path
        combined_msg = "\n".join(msg)
        with open(file_path, "a") as log:
            log.write(combined_msg + "\n")

    def _log_daos_metrics(self):
        """Get and log the daos_metrics for each server."""
        metrics_dir = os.path.join(self._performance_log_dir, "daos_metrics")
        os.makedirs(metrics_dir, exist_ok=True)
        per_engine_results = self.server_managers[0].get_daos_metrics()
        for engine_idx, engine_results in enumerate(per_engine_results):
            for host_results in engine_results:
                log_name = "{}_engine{}.csv".format(host_results["hosts"], engine_idx)
                log_path = os.path.join(metrics_dir, log_name)
                self.log_performance(host_results["stdout"], False, log_path)

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
            group (str): ior or mdtest
            extra_params (list, optional): extra params to print. Default is None.

        """
        group = group.upper()
        if group not in ("IOR", "MDTEST"):
            self.fail("Invalid group: {}".format(group))

        # Start with common parameters
        # Build a list of [PARAM_NAME, PARAM_VALUE]
        params = [
            ["TEST_ID", self.unique_id],
            ["TEST_NAME", self.test_id],
            ["TEST_GROUP", group],
            ["NUM_SERVERS", self.num_servers],
            ["NUM_TARGETS", self.num_targets],
            ["NUM_CLIENTS", self.num_clients],
            ["PPC", self.ppn],
            ["PPN", self.ppn]
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
        max_len = max([len(param[0]) for param in params])
        self.log_performance(
            ["{:<{}} : {}".format(param[0], max_len, param[1]) for param in params]
        )

    def set_processes_ppn(self, namespace):
        """Set processes and ppn from a given namespace.

        Args:
            namespace (str): config namespace. E.g. /run/ior/*

        """
        self.processes = self.params.get("np", namespace, self.processes)
        self.ppn = self.params.get("ppn", namespace, self.ppn)

    def verify_system_status(self):
        """Verify container/pool/system status (in that order)."""
        if self.pool and self.container:
            self.log.info(self.daos_cmd.container_query(self.pool.uuid, self.container.uuid))
        if self.pool:
            self.display_pool_space()
        self.dmg_cmd.system_query()

    def verify_oclass_server_count(self, oclass):
        """Verify an object class is compatible with the number of servers.

        Args:
            oclass (str): The object class. Assumed to be valid.

        """
        min_servers = oclass_utils.calculate_min_servers(oclass)
        if self.num_servers < min_servers:
            self.fail("Need at least {} servers for oclass {}".format(min_servers, oclass))

    def restart_servers(self):
        """Restart the servers."""
        self.log.info("Restarting servers")
        self.dmg_cmd.system_stop(True)
        if self.dmg_cmd.result.exit_status != 0:
            self.fail("Failed to stop servers")
        time.sleep(5)
        self.dmg_cmd.system_start()
        if self.dmg_cmd.result.exit_status != 0:
            self.fail("Failed to start servers")
        self.server_managers[0].detect_engine_start()

    def _run_performance_ior_single(self, stop_rank_s=None, intercept=None):
        """Run a single IOR execution.

        Args:
            stop_rank_s (float, optional): stop a rank this many seconds after starting IOR.
                Default is None, which does not stop a rank.
            intercept (str, optional): path to interception library.

        """
        # Always run as a subprocess so we can stop ranks during IO
        self.subprocess = True

        self.run_ior_with_pool(
            create_pool=False,
            create_cont=False,
            intercept=intercept,
            display_space=False,
            stop_dfuse=False
        )
        if stop_rank_s is not None:
            time.sleep(stop_rank_s)
            self.server_managers[0].stop_random_rank(self.d_log, force=True, exclude_ranks=[0])
        ior_returncode = self.job_manager.process.wait()
        try:
            if ior_returncode != 0:
                self.fail("IOR failed")
            ior_output = get_subprocess_stdout(self.job_manager.process)
            ior_metrics = self.ior_cmd.get_ior_metrics(ior_output)
            for metrics in ior_metrics:
                if metrics[0] == "write":
                    self.log_performance("Max Write: {}".format(metrics[IorMetrics.Max_MiB]))
                elif metrics[0] == "read":
                    self.log_performance("Max Read: {}".format(metrics[IorMetrics.Max_MiB]))
        finally:
            # Try this even if IOR failed because it could give us useful info
            self.verify_system_status()

    def run_performance_ior(self, namespace=None, use_intercept=True, stop_delay_write=None,
                            stop_delay_read=None, num_iterations=1,
                            restart_between_iterations=True):
        """Run an IOR performance test.

        Write and Read are ran separately.

        Args:
            namespace (str, optional): namespace for IOR parameters in the yaml.
                Defaults to None, which uses default IOR namespace.
            use_intercept (bool, optional): whether to use the interception library with dfuse.
                Defaults to True.
            stop_delay_write (float, optional): fraction of stonewall time after which to stop a
                rank during write phase. Must be between 0 and 1. Default is None.
            stop_delay_read (float, optional): fraction of stonewall time after which to stop a
                rank during read phase. Must be between 0 and 1. Default is None.
            num_iterations (int, optional): number of times to run the tests.
                Default is 1.
            restart_between_iterations (int, optional): whether to restart the servers between
                iterations. Default is True.

        """
        if stop_delay_write is not None and (stop_delay_write < 0 or stop_delay_write > 1):
            self.fail("stop_delay_write must be between 0 and 1")
        if stop_delay_read is not None and (stop_delay_read < 0 or stop_delay_read > 1):
            self.fail("stop_delay_read must be between 0 and 1")
        if stop_delay_write is not None and stop_delay_read is not None:
            # This isn't straightforward, because stopping a rank during write degrades
            # performance, so read tries to read the same number of bytes as write,
            # but might finish before the rank is stopped.
            self.fail("stop_delay_write and stop_delay_read cannot be used together")

        if namespace is not None:
            self.ior_cmd.namespace = namespace
            self.ior_cmd.get_params(self)
            self.set_processes_ppn(namespace)

        if use_intercept and self.ior_cmd.api.value == 'POSIX':
            intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        else:
            intercept = None

        # Calculate both stop delays upfront since read phase will remove stonewall
        stop_rank_write_s = stop_rank_read_s = None
        if stop_delay_write and self.ior_cmd.sw_deadline.value:
            stop_rank_write_s = stop_delay_write * self.ior_cmd.sw_deadline.value
        if stop_delay_read and self.ior_cmd.sw_deadline.value:
            stop_rank_read_s = stop_delay_read * self.ior_cmd.sw_deadline.value

        # Save write and read params for switching
        write_flags = self.params.get("write_flags", self.ior_cmd.namespace)
        read_flags = self.params.get("read_flags", self.ior_cmd.namespace)
        if write_flags is None:
            self.fail("write_flags not found in config")
        if read_flags is None:
            self.fail("read_flags not found in config")
        write_sw_wearout = self.ior_cmd.sw_wearout.value
        write_sw_deadline = self.ior_cmd.sw_deadline.value

        self._log_performance_params(
            "ior",
            [["IOR Write Flags", write_flags],
             ["IOR Read Flags", read_flags]])

        self.verify_oclass_server_count(self.ior_cmd.dfs_oclass.value)

        # Set the container redundancy factor to match the oclass
        cont_rf = oclass_utils.extract_redundancy_factor(self.ior_cmd.dfs_oclass.value)

        # Create pool and container upfront for flexibility and so rank stop timing is accurate
        self.create_pool()
        self.container = self.get_container(
            self.pool, extra_props="rf:{}".format(cont_rf),
            chunk_size=self.ior_cmd.dfs_chunk.value, oclass=self.ior_cmd.dfs_oclass.value)
        self.update_ior_cmd_with_pool(False)

        for iteration in range(num_iterations):
            if restart_between_iterations and iteration > 0:
                self.restart_servers()

            self.log.info("Running IOR write (%s)", str(iteration))
            self.ior_cmd.flags.update(write_flags)
            self.ior_cmd.sw_wearout.update(write_sw_wearout)
            self.ior_cmd.sw_deadline.update(write_sw_deadline)
            self._run_performance_ior_single(stop_rank_write_s, intercept)

            # Manually stop dfuse after ior write completes
            self.stop_dfuse()

            # Wait for rebuild if we stopped a rank
            if stop_rank_write_s:
                self.pool.wait_for_rebuild(False)

            # Wait between write and read
            self.phase_barrier()

            self.log.info("Running IOR read (%s)", str(iteration))
            self.ior_cmd.flags.update(read_flags)
            self.ior_cmd.sw_wearout.update(None)
            self.ior_cmd.sw_deadline.update(None)
            self._run_performance_ior_single(stop_rank_read_s, intercept)

            # Manually stop dfuse after ior read completes
            self.stop_dfuse()

            # Wait for rebuild if we stopped a rank
            if stop_rank_read_s:
                self.pool.wait_for_rebuild(False)

        self._log_daos_metrics()

    def run_performance_mdtest(self, namespace=None, stop_delay=None):
        """Run an MDTest performance test.

        Args:
            namespace (str, optional): namespace for MDTest parameters in the yaml.
                Defaults to None, which uses default MDTest namespace.
            stop_delay (float, optional): fraction of stonewall time after which to stop a
                rank. Must be between 0 and 1. Defaults to None.

        """
        if stop_delay is not None and (stop_delay < 0 or stop_delay > 1):
            self.fail("stop_delay must be between 0 and 1")

        if namespace is not None:
            self.mdtest_cmd.namespace = namespace
            self.mdtest_cmd.get_params(self)
            self.set_processes_ppn(namespace)

        # Performance with POSIX/DFUSE is tricky because we can't just set
        # dfs_dir_oclass and dfs_oclass. This needs more work before running non-DFS.
        if self.mdtest_cmd.api.value != 'DFS':
            self.fail("Only DFS API supported")

        stop_rank_s = (stop_delay or 0) * (self.mdtest_cmd.stonewall_timer.value or 0)

        self._log_performance_params("mdtest")

        self.verify_oclass_server_count(self.mdtest_cmd.dfs_oclass.value)
        self.verify_oclass_server_count(self.mdtest_cmd.dfs_dir_oclass.value)

        # Set the container redundancy factor to match the oclass used
        cont_rf = min([
            oclass_utils.extract_redundancy_factor(self.mdtest_cmd.dfs_oclass.value),
            oclass_utils.extract_redundancy_factor(self.mdtest_cmd.dfs_dir_oclass.value)])

        # Create pool and container upfront so rank stop timing is more accurate
        self.add_pool(connect=False)
        self.add_container(self.pool, create=False)
        properties = "rf:{}".format(cont_rf)
        current_properties = self.container.properties.value
        if current_properties:
            new_properties = current_properties + "," + properties
        else:
            new_properties = properties
        self.container.properties.update(new_properties)
        self.container.create()

        # Never let execute_mdtest automatically destroy the container
        self.mdtest_cmd.dfs_destroy.update(False)

        # Always run as a subprocess so we can stop ranks during IO
        self.subprocess = True

        self.log.info("Running MDTEST")
        self.execute_mdtest()
        if stop_rank_s:
            time.sleep(stop_rank_s)
            self.server_managers[0].stop_random_rank(self.d_log, force=True, exclude_ranks=[0])
        mdtest_returncode = self.job_manager.process.wait()
        try:
            if mdtest_returncode != 0:
                self.fail("mdtest failed")
            mdtest_output = get_subprocess_stdout(self.job_manager.process)
            mdtest_metrics = MdtestMetrics(mdtest_output)
            if not mdtest_metrics:
                self.fail("Failed to get mdtest metrics")
            self.log_performance([
                "create_ops: {}".format(mdtest_metrics.rates.file_creation.max),
                "stat_ops: {}".format(mdtest_metrics.rates.file_stat.max),
                "read_ops: {}".format(mdtest_metrics.rates.file_read.max),
                "remove_ops: {}".format(mdtest_metrics.rates.file_removal.max)
            ])
        finally:
            # Try this even if MDTest failed because it could give us useful info
            self.verify_system_status()

        # Manually stop dfuse after mdtest completes
        self.stop_dfuse()

        # Wait for rebuild if we stopped a rank
        if stop_rank_s:
            self.pool.wait_for_rebuild(False)

        self._log_daos_metrics()
