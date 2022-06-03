#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re
from ior_test_base import IorTestBase
from telemetry_test_base import TestWithTelemetry
from telemetry_utils import TelemetryUtils
from test_utils_container import TestContainer


def get_rf(oclass):
    """Return redundancy factor based on the oclass.

    Args:
        oclass(string): object class.

    return:
        redundancy factor(int) from object type
    """
    rf = 0
    if "EC" in oclass:
        tmp = re.findall(r'\d+', oclass)
        if tmp:
            rf = int(tmp[1])
    elif "RP" in oclass:
        tmp = re.findall(r'\d+', oclass)
        if tmp:
            rf = int(tmp[0]) - 1
    else:
        rf = 0
    return rf


def convert_to_number(size):
    """Convert string to int.

    Args:
        size (str): String from yaml that represents a string with suffix
                    denoting how many bytes. suffix must be B, K, M, G or T
    Returns:
        num: (int) converted string number in bytes

    """
    num = 0
    SIZE_DICT = {"B": 1,
                 "K": 1024,
                 "M": 1024 * 1024,
                 "G": 1024 * 1024 * 1024,
                 "T": 1024 * 1024 * 1024 * 1024}
    # Convert string to bytes
    suffix = str(size)[-1]
    for key, value in SIZE_DICT.items():
        if suffix == key:
            num = int(value) * int(size[:-1])
    return int(num)


class TestWithTelemetryIOLatency(IorTestBase, TestWithTelemetry):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Test telemetry engine io basic metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super().__init__(*args, **kwargs)
        self.rpc_latency = {}
        self.iterations = 1

    def add_containers(self, pool, oclass=None, path="/run/container/*"):
        """Create a list of containers that the various jobs use for storage.

        Args:
            pool: pool to create container
            oclass: object class of container


        """
        rf = None
        # Create a container and add it to the overall list of containers
        self.container.append(
            TestContainer(pool, daos_command=self.get_daos_command()))
        self.container[-1].namespace = path
        self.container[-1].get_params(self)
        # include rf based on the class
        if oclass:
            self.container[-1].oclass.update(oclass)
            redundancy_factor = get_rf(oclass)
            rf = 'rf:{}'.format(str(redundancy_factor))
        properties = self.container[-1].properties.value
        cont_properties = (",").join(filter(None, [properties, rf]))
        if cont_properties is not None:
            self.container[-1].properties.update(cont_properties)
        self.container[-1].create()

    def get_ior_latency(self, cmdresult):
        """Get the ior command read and write results from stdout.

        Parse the CmdResult (output of the test) and look for the ior stdout
        and get the read and write perf data.

        Args:
            cmdresult (CmdResult): output of job manager

        Returns:
            latency(list) : list of read or write latency

        """
        latency = []
        ior_metric_summary = "Results: "
        messages = cmdresult.stdout_text.splitlines()
        # Get the index where the summary starts and add one to
        # get to the header.
        idx = messages.index(ior_metric_summary)
        # idx + 1, idx + 2  and idx + 3  are headers.
        # idx + 4 will give ior perf info.
        for iteration in range(self.iterations):
            ior_results = (" ".join(messages[idx+4+iteration].split())).split()
            # Latency will not include open and close time in order to compare to the
            # IO rpc latency reported by daos metrics
            # ior_results is a list of the following:
            # Results:

            # access bw(MiB/s) IOPS Latency(s) block xfer(KiB) open(s)  wr/rd(s) close(s) total(s)
            # ------ --------- ---- ---------- ----- --------- -------- -------- -------- --------
            # read   107.84    27.1 0.036824   4096  4096      0.000252 0.036824 0.000015 0.037091
            self.log.info(
                "Latency for ior %s with transfer size %s(KiB) is %.2fus"
                "", ior_results[0], ior_results[5], (float(ior_results[3])*float(10**6)))
            latency.append(float(ior_results[3])*float(10**6))
        return latency

    def verify_ior_latency_metrics(self, metrics, ior_latency, test_metric, transfer_size):
        """Verify latency metric against ior latency from job results.

        Args:
            metrics (dict): dictionary of io latency metrics
            ior_latency (dict): dictionary of list of ior latency
            test_metric (str): name of telemetry metrics
            transfer_size(str): transfer_size

        Returns:
            status: (bool) True if metric is verified

        """
        status = {}
        status[test_metric] = False
        if convert_to_number(transfer_size) > convert_to_number("4M"):
            size = "GT4MB"
        else:
            size = transfer_size + "B"
        # collecting data to be verified
        latency = 0
        self.rpc_latency[test_metric] = 0
        if "update" in test_metric:
            operation = "update"
            ior_operation = "write"
        else:
            operation = "fetch"
            ior_operation = "read"
        # add up metrics from ior write/read with transfer size = transfer_size
        # across all servers, ranks, targets
        for host in self.hostlist_servers:
            # test assumes one engine per host
            rank = self.server_managers[-1].get_host_ranks([host])[0]
            for target in range(self.server_managers[-1].get_config_value("targets")):
                value = metrics[test_metric][host][str(rank)][str(target)][size]
                latency = latency + value
        self.rpc_latency[test_metric] = latency
        # Verify latency against IOR latency
        if test_metric in ["engine_io_latency_fetch", "engine_io_latency_update"]:
            self.log.info(
                "IO %s RPC latency = %.2f for %s transfer size"
                "", operation, self.rpc_latency[test_metric], size)
            self.log.info(
                "IOR %s latency = %.2f for %s transfer size"
                "", ior_operation, ior_latency[transfer_size][operation][-1], size)
            if float(
                  self.rpc_latency[test_metric]) < float(ior_latency[transfer_size][operation][-1]):
                status[test_metric] = True
        # Verify max latency against max IOR latency
        if test_metric in ["engine_io_latency_fetch_max", "engine_io_latency_update_max"]:
            max_ior_latency = max(ior_latency[transfer_size][operation])
            self.log.info(
                "IO %s RPC max latency = %.2f for %s transfer size"
                "", operation, self.rpc_latency[test_metric], size)
            self.log.info(
                "IOR %s max latency = %.2f for %s transfer size"
                "", ior_operation, max_ior_latency, size)
            if float(self.rpc_latency[test_metric]) < float(max_ior_latency):
                status[test_metric] = True
        # Verify min latency against min IOR latency
        if test_metric in ["engine_io_latency_fetch_min", "engine_io_latency_update_min"]:
            min_ior_latency = min(ior_latency[transfer_size][operation])
            self.log.info(
                "IO %s RPC min latency = %.2f for %s transfer size"
                "", operation, self.rpc_latency[test_metric], size)
            self.log.info(
                "IOR %s min latency = %.2f for %s transfer size"
                "", ior_operation, min_ior_latency, size)
            if float(self.rpc_latency[test_metric]) < float(min_ior_latency):
                status[test_metric] = True
        # Verify mean latency against mean IOR latency
        if test_metric in ["engine_io_latency_fetch_mean", "engine_io_latency_update_mean"]:
            temp = 0
            for idx in range(self.iterations):
                temp = temp + ior_latency[transfer_size][operation][idx]
            ior_mean = float(temp)/float(self.iterations)
            self.log.info(
                "IO %s RPC mean latency = %.2f for %s transfer size"
                "", operation, self.rpc_latency[test_metric], size)
            self.log.info(
                "IOR %s mean latency = %.2f for %s transfer size"
                "", ior_operation, ior_mean, size)
            if float(self.rpc_latency[test_metric]) < float(ior_mean):
                status[test_metric] = True
        if test_metric in ["engine_io_latency_fetch_stddev", "engine_io_latency_update_stddev"]:
            status[test_metric] = True
        return status[test_metric]

    def verify_rpc_latency_metrics(self, metrics_data, test_metrics, transfer_size):
        """Verify latency metric against ior latency from job results.

        Args:
            metrics_data (dict): dictionary of io latency metrics
            test_metric (str): name of telemetry metrics
            transfer_size(str): transfer_size
        Returns:
            status: (dict) Dictionary of status if io rpc metrics are verified

        """
        if convert_to_number(transfer_size) > convert_to_number("4M"):
            size = "GT4MB"
        else:
            size = transfer_size + "B"
        status = {}
        metrics = {}
        # sum up the latency values
        for test_metric in test_metrics:
            metrics[test_metric] = 0
            for host in self.hostlist_servers:
                # test assumes one engine per host
                for rank in self.server_managers[-1].get_host_ranks([host]):
                    for target in range(self.server_managers[-1].get_config_value("targets")):
                        value = metrics_data[test_metric][host][str(rank)][str(target)][size]
                        metrics[test_metric] = metrics[test_metric] + value
        min_value = metrics["engine_io_latency_fetch_min"]
        max_value = metrics["engine_io_latency_fetch_max"]
        mean_value = metrics["engine_io_latency_fetch_mean"]
        stddev_value = metrics["engine_io_latency_fetch_stddev"]
        if ((max_value >= metrics["engine_io_latency_fetch"] >= min_value) and (
                    max_value > mean_value > min_value) and (
                        stddev_value < (max_value-min_value))):
            status["fetch"] = True

        min_value = metrics["engine_io_latency_update_min"]
        max_value = metrics["engine_io_latency_update_max"]
        mean_value = metrics["engine_io_latency_update_mean"]
        stddev_value = metrics["engine_io_latency_update_stddev"]
        if ((max_value >= metrics["engine_io_latency_update"] >= min_value) and (
                    max_value > mean_value > min_value) and (
                        stddev_value < (max_value-min_value))):
            status["update"] = True
        return status

    def test_io_latency_telmetry_metrics(self):
        """JIRA ID: DAOS-8624.

            Create files with transfers sizes 512 to 4M to verify the
            DAOS engine IO latency telemetry metrics min, max, mean and stddev.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=telemetry
        :avocado: tags=test_io_latency_telemetry

        """
        transfer_sizes = self.params.get("transfer_sizes", "/run/*")
        self.iterations = self.params.get("repetitions", "/run/*")
        self.container = []
        verification_results = []
        metrics_data = {}
        ior_latency = {}
        # disable verbosity
        self.telemetry.dmg.verbose = False
        test_metrics = TelemetryUtils.ENGINE_IO_LATENCY_FETCH_METRICS + \
                       TelemetryUtils.ENGINE_IO_LATENCY_UPDATE_METRICS

        for transfer_size in transfer_sizes:
            ior_latency[transfer_size] = {}
            self.add_pool(connect=False)
            oclass = self.ior_cmd.dfs_oclass.value
            self.add_containers(self.pool, oclass)
            for operation in ["update", "fetch"]:
                flags = self.params.get("F", "/run/ior/ior{}flags/".format(
                    operation))
                self.log.info(
                    "<<< Start ior %s transfer_size=%s", operation, transfer_size)
                self.ior_cmd.transfer_size.update(transfer_size)
                self.ior_cmd.flags.update(flags)
                self.ior_cmd.set_daos_params(
                    self.server_group, self.pool, self.container[-1].uuid)
                # Run ior command
                ior_results = self.run_ior_with_pool(
                        timeout=200, create_pool=False, create_cont=False)
                ior_latency[transfer_size][operation] = self.get_ior_latency(ior_results)
                if operation in "update":
                    metrics_data.update(self.telemetry.get_io_metrics(
                        TelemetryUtils.ENGINE_IO_LATENCY_UPDATE_METRICS))
                else:
                    metrics_data.update(self.telemetry.get_io_metrics(
                        TelemetryUtils.ENGINE_IO_LATENCY_FETCH_METRICS))
            # Destroy the container and the pool.
            self.destroy_containers(containers=self.container[-1])
            self.destroy_pools(pools=self.pool)

        for transfer_size in transfer_sizes:
            status_dict = self.verify_rpc_latency_metrics(
                metrics_data, test_metrics, str(transfer_size))
            for operation in ["update", "fetch"]:
                if status_dict[operation]:
                    verification_results.append(
                        ["PASSED", "engine_io_latency_" + operation, transfer_size])
                else:
                    verification_results.append(
                        ["FAILED", "engine_io_latency_" + operation, transfer_size])
        errors = False
        self.log.error("Summary of io latency min, max, mean and stddev comparison results:")
        for item in verification_results:
            self.log.info("  %s  %s  %s", item[0], item[1], item[2])
            if item[0] == "FAILED":
                errors = True
        if errors:
            self.fail("Test FAILED")

    def get_initial_io_dtx_committed_metrics(self, metrics_data, test_metrics,
                                             transfer_size):
        """Get baseline IO dtx metrics before running IOR.

        Args:
            metrics_data (dict): dictionary of io dtx "committed" metrics
            test_metrics (dict): io dtx "committed" telemetry metrics
            transfer_size(str): transfer_size used with ior

        Returns:
            status: (bool) True if metrics are verified for transfer size

        """
        metrics = {}
        for test_metric in test_metrics:
            metrics[test_metric] = 0
            for host in self.hostlist_servers:
                # test assumes one engine per host
                for rank in self.server_managers[-1].get_host_ranks([host]):
                    for target in range(self.server_managers[-1].get_config_value("targets")):
                        value = metrics_data[test_metric][host][str(rank)][str(target)]["-"]
                        metrics[test_metric] = metrics[test_metric] + value
        dtx_value = metrics["engine_io_dtx_committed"]
        min_value = metrics["engine_io_dtx_committed_min"]
        max_value = metrics["engine_io_dtx_committed_max"]
        mean_value = metrics["engine_io_dtx_committed_mean"]
        stddev_value = metrics["engine_io_dtx_committed_stddev"]

        self.log.info("Initial baseline stats for DTX for transfer size %s", transfer_size)
        self.log.info("engine_io_dtx_committed = %s", dtx_value)
        self.log.info("engine_io_dtx_committed_min = %s", min_value)
        self.log.info("engine_io_dtx_committed_max = %s", max_value)
        self.log.info("engine_io_dtx_committed_mean = %s", mean_value)
        self.log.info("engine_io_dtx_committed_stddev = %s", stddev_value)

        return metrics

    def verify_io_dtx_committed_metrics(self, baseline_data, metrics_data, test_metrics,
                                        transfer_size):
        """Verify IO dtx metrics after running IOR.

        Args:
            baseline_data (dict): dictionary of the initial io dtx "committed" metrics
                                  before IOR run
            metrics_data (dict): dictionary of io dtx "committed" metrics
            test_metrics (dict): io dtx "committed" telemetry metrics
            transfer_size(str): transfer_size used with ior

        Returns:
            status: (bool) True if metrics are verified for transfer size

        """
        block_size = self.params.get("block_size", "/run/*")
        repetitions = self.params.get("repetitions", "/run/*")
        test_ops = 3
        status = True

        metrics = {}
        for test_metric in test_metrics:
            metrics[test_metric] = 0
            for host in self.hostlist_servers:
                # test assumes one engine per host
                for rank in self.server_managers[-1].get_host_ranks([host]):
                    for target in range(self.server_managers[-1].get_config_value("targets")):
                        value = metrics_data[test_metric][host][str(rank)][str(target)]["-"]
                        metrics[test_metric] = metrics[test_metric] + value

        # TODO: calculate diff from initial baseline test value for
        # committed and max metrics with DAOS-9003
        dtx_value = metrics["engine_io_dtx_committed"]
        #dtx_value -= baseline_data["engine_io_dtx_committed"]
        max_value = metrics["engine_io_dtx_committed_max"]
        #max_value -= baseline_data["engine_io_dtx_committed_max"]
        # expected min value after the test should be 1
        min_value = metrics["engine_io_dtx_committed_min"]
        # TODO: Validate stddev and mean value metrics
        # mean and std dev metrics only rely on the after test values
        # mean_value = metrics["engine_io_dtx_committed_mean"]
        # stddev_value = metrics["engine_io_dtx_committed_stddev"]

        # DTX committed value should be equal to the number of transactions.
        if transfer_size == "512":
            transfer_size = "512B"

        # num operations = (blocksize / xfersize) * repetitions + ops for create/remove
        num_operations = convert_to_number(block_size) / convert_to_number(transfer_size)
        num_operations *= repetitions
        # test ops = (create dir + punch obj + punch dir) * repetitions
        test_ops *= repetitions
        num_operations += test_ops

        if dtx_value != num_operations:
            self.log.error("engine_io_dtx_committed NOT verified, %s != %s",
                           dtx_value, num_operations)
            status = False
        # TODO: Update min value to 1 with DAOS-9003
        if min_value != 0:
            self.log.error("engine_io_dtx_committed_min != 0")
            status = False
        if not max_value >= dtx_value >= min_value:
            self.log.error("!(engine_io_dtx_committed_max >= committed >= min)")
            status = False

        return status

    def test_ior_dtx_telemetry_metrics(self):
        """JIRA ID: DAOS-8973.

            Create files with transfers sizes 512 to 4M to verify the
            DAOS engine IO DTX telemetry metrics infrastructure and
            verify values using IOR.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=telemetry,control
        :avocado: tags=test_ior_dtx_telemetry

        """
        transfer_sizes = self.params.get("transfer_sizes", "/run/*")
        self.iterations = self.params.get("repetitions", "/run/*")
        self.container = []
        metrics_data = {}
        baseline_data = {}
        # disable verbosity
        self.telemetry.dmg.verbose = False
        committed_test_metrics = TelemetryUtils.ENGINE_IO_DTX_COMMITTED_METRICS
        #TODO: DAOS-9564: Verify I/O dtx committable metrics
        #committable_test_metrics = TelemetryUtils.ENGINE_IO_DTX_COMMITTABLE_METRICS

        for transfer_size in transfer_sizes:
            # Get the initial IO dtx metrics before running
            metrics_data.update(self.telemetry.get_io_metrics(
                TelemetryUtils.ENGINE_IO_DTX_COMMITTED_METRICS))
            baseline_data = self.get_initial_io_dtx_committed_metrics(metrics_data,
                                                                      committed_test_metrics,
                                                                      str(transfer_size))
            self.add_pool(connect=False)
            oclass = self.ior_cmd.dfs_oclass.value
            self.add_containers(self.pool, oclass)
            for operation in ["rw"]:
                flags = self.params.get("F", "/run/ior/ior{}flags/".format(
                    operation))
                self.log.info(
                        "<<< Start ior %s transfer_size=%s", operation, transfer_size)
                self.ior_cmd.transfer_size.update(transfer_size)
                self.ior_cmd.flags.update(flags)
                self.ior_cmd.set_daos_params(
                        self.server_group, self.pool, self.container[-1].uuid)
                # Run ior command to populate IO dtx metrics
                _ = self.run_ior_with_pool(
                        timeout=200, create_pool=False, create_cont=False)
                # _ = self.ior_with_transfer_size(transfer_size, operation)
                # Get IO dtx telemetry metrics
                metrics_data.update(self.telemetry.get_io_metrics(
                    TelemetryUtils.ENGINE_IO_DTX_COMMITTED_METRICS))

                errors = False
                # Verify IO dtx committed metrics
                if self.verify_io_dtx_committed_metrics(baseline_data, metrics_data,
                                                        committed_test_metrics,
                                                        str(transfer_size)):
                    self.log.info("IO dtx committed metrics verified for xfer size %s",
                                  transfer_size)
                else:
                    self.log.error("IO dtx committed metrics failed verification for xfer size %s",
                                   transfer_size)
                    errors = True
            # Destroy the container and the pool.
            self.destroy_containers(containers=self.container[-1])
            self.destroy_pools(pools=self.pool)

        if errors:
            self.fail("Test FAILED")

    def test_ior_latency_telmetry_metrics(self):
        """JIRA ID: DAOS-8624.

            Create files with transfers sizes 512 to 4M to verify the
            DAOS engine IO latency telemetry metrics infrastructure and
            verify latency against the ior latency.  It is assumed that rpc io
            latency should be less than the ior latency reported for each transfer
            size.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=telemetry
        :avocado: tags=test_ior_latency_telemetry

        """
        transfer_sizes = self.params.get("transfer_sizes", "/run/*")
        self.iterations = self.params.get("repetitions", "/run/*")
        self.container = []
        ior_verification_results = []
        metrics_data = {}
        ior_latency = {}
        # disable verbosity
        self.telemetry.dmg.verbose = False
        test_metrics = TelemetryUtils.ENGINE_IO_LATENCY_FETCH_METRICS + \
                       TelemetryUtils.ENGINE_IO_LATENCY_UPDATE_METRICS

        for transfer_size in transfer_sizes:
            ior_latency[transfer_size] = {}
            self.add_pool(connect=False)
            oclass = self.ior_cmd.dfs_oclass.value
            self.add_containers(self.pool, oclass)
            for operation in ["update", "fetch"]:
                flags = self.params.get("F", "/run/ior/ior{}flags/".format(
                    operation))
                self.log.info(
                    "<<< Start ior %s transfer_size=%s", operation, transfer_size)
                self.ior_cmd.transfer_size.update(transfer_size)
                self.ior_cmd.flags.update(flags)
                self.ior_cmd.set_daos_params(
                    self.server_group, self.pool, self.container[-1].uuid)
                # Run ior command
                ior_results = self.run_ior_with_pool(
                        timeout=200, create_pool=False, create_cont=False)
                ior_latency[transfer_size][operation] = self.get_ior_latency(ior_results)
                if operation in "update":
                    metrics_data.update(self.telemetry.get_io_metrics(
                        TelemetryUtils.ENGINE_IO_LATENCY_UPDATE_METRICS))
                else:
                    metrics_data.update(self.telemetry.get_io_metrics(
                        TelemetryUtils.ENGINE_IO_LATENCY_FETCH_METRICS))
            # Destroy the container and the pool.
            self.destroy_containers(containers=self.container[-1])
            self.destroy_pools(pools=self.pool)

        # check dmg latency metrics against ior latency metrics
        for test_metric in test_metrics:
            for transfer_size in transfer_sizes:
                if self.verify_ior_latency_metrics(
                        metrics_data, ior_latency, test_metric, str(transfer_size)):
                    ior_verification_results.append(["PASSED", test_metric, transfer_size])
                else:
                    ior_verification_results.append(["FAILED", test_metric, transfer_size])
        # check engine io latency rpc min, max, mean and stddev values for each transfer size
        errors = False
        self.log.error("Summary of io latency test results:")
        # Check ior results
        for item in ior_verification_results:
            self.log.info("  %s  %s  %s", item[0], item[1], item[2])
            if item[0] == "FAILED":
                errors = True

        if errors:
            self.fail("Test FAILED")
