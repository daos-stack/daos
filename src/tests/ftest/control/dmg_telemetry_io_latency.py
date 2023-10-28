"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from general_utils import report_errors
from ior_test_base import IorTestBase
from oclass_utils import extract_redundancy_factor
from telemetry_test_base import TestWithTelemetry
from telemetry_utils import TelemetryUtils


def convert_to_number(size):
    """Convert string to int.

    Args:
        size (str): String from yaml that represents a string with suffix
                    denoting how many bytes. suffix must be B, K, M, G or T
    Returns:
        num: (int) converted string number in bytes

    """
    num = 0
    size_dict = {"B": 1,
                 "K": 1024,
                 "M": 1024 * 1024,
                 "G": 1024 * 1024 * 1024,
                 "T": 1024 * 1024 * 1024 * 1024}
    # Convert string to bytes
    suffix = str(size)[-1]
    for key, value in size_dict.items():
        if suffix == key:
            num = int(value) * int(size[:-1])
    return int(num)


class TestWithTelemetryIOLatency(IorTestBase, TestWithTelemetry):
    # pylint: disable=too-many-nested-blocks
    """Test telemetry engine io basic metrics.

    :avocado: recursive
    """

    def add_containers(self, pool, oclass, path="/run/container/*"):
        """Create a list of containers that the various jobs use for storage.

        Args:
            pool (TestPool): pool to create container
            oclass (str): object class of container
            path (str): container namespace path

        """
        # Create a container and add it to the overall list of containers
        self.container.append(self.get_container(pool, namespace=path, create=False))
        # include rd_fac based on the class
        self.container[-1].oclass.update(oclass)
        redundancy_factor = extract_redundancy_factor(oclass)
        rd_fac = 'rd_fac:{}'.format(str(redundancy_factor))
        properties = self.container[-1].properties.value
        cont_properties = ",".join(filter(None, [properties, rd_fac]))
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
        for iteration in range(self.ior_cmd.repetitions.value):
            ior_results = (" ".join(messages[idx + 4 + iteration].split())).split()
            # Latency will not include open and close time in order to compare to the
            # IO rpc latency reported by daos metrics
            # ior_results is a list of the following:
            # Results:

            # access bw(MiB/s) IOPS Latency(s) block xfer(KiB) open(s)  wr/rd(s) close(s) total(s)
            # ------ --------- ---- ---------- ----- --------- -------- -------- -------- --------
            # read   107.84    27.1 0.036824   4096  4096      0.000252 0.036824 0.000015 0.037091
            self.log.info(
                "Latency for ior %s with transfer size %s(KiB) is %.2fus"
                "", ior_results[0], ior_results[5], (float(ior_results[3]) * float(10**6)))
            latency.append(float(ior_results[3]) * float(10**6))
        return latency

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
                max_value > mean_value > min_value) and (stddev_value < (max_value - min_value))):
            status["fetch"] = True

        min_value = metrics["engine_io_latency_update_min"]
        max_value = metrics["engine_io_latency_update_max"]
        mean_value = metrics["engine_io_latency_update_mean"]
        stddev_value = metrics["engine_io_latency_update_stddev"]
        if ((max_value >= metrics["engine_io_latency_update"] >= min_value) and (
                max_value > mean_value > min_value) and (stddev_value < (max_value - min_value))):
            status["update"] = True
        return status

    def test_io_latency_telmetry_metrics(self):
        """JIRA ID: DAOS-8624.

            Create files with transfers sizes 512 to 4M to verify the
            DAOS engine IO latency telemetry metrics min, max, mean and stddev.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,telemetry,daos_cmd
        :avocado: tags=TestWithTelemetryIOLatency,test_io_latency_telmetry_metrics
        """
        transfer_sizes = self.params.get("transfer_sizes", "/run/*")
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
            self.add_containers(self.pool, oclass=self.ior_cmd.dfs_oclass.value)
            for operation in ["update", "fetch"]:
                flags = self.params.get("F", "/run/ior/ior{}flags/".format(operation))
                self.log.info("<<< Start ior %s transfer_size=%s", operation, transfer_size)
                self.ior_cmd.transfer_size.update(transfer_size)
                self.ior_cmd.flags.update(flags)
                self.ior_cmd.set_daos_params(
                    self.server_group, self.pool, self.container[-1].identifier)
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
        errors = []
        self.log.info("Summary of io latency min, max, mean and stddev comparison results:")
        for item in verification_results:
            msg = "  {} {} {}".format(item[0], item[1], item[2])
            self.log.info(msg)
            if item[0] == "FAILED":
                errors.append(msg)
        report_errors(self, errors)
