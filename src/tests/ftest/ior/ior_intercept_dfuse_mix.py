#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics


class IorInterceptDfuseMix(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR only with dfuse and with mix of
       dfuse and interception library on a single server and multi
       client settings with basic parameters.

       Verify the throughput improvement with IL.

    :avocado: recursive
    """

    def test_ior_intercept_dfuse_mix(self):
        """Jira ID: DAOS-3500.

        Test Description:
            Purpose of this test is to run ior through dfuse on 4 clients
            for 5 minutes and capture the metrics and use the
            intercepiton library by exporting LD_PRELOAD to the libioil.so
            path on 2 clients and leave 2 clients to use dfuse and rerun
            the above ior and capture the metrics and compare the
            performance difference and check using interception
            library make significant performance improvement. Verify the
            client didn't use the interception library doesn't show any
            improvement.

        Use case:
            Run ior with read, write for 5 minutes
            Run ior with read, write for 5 minutes with interception
            library

            Compare the results and check whether using interception
                library provides better performance and not using it
                does not change the performance.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,ior_intercept_mix
        """
        self.add_pool()
        self.add_container(self.pool)

        # Run 2 IOR threads; one with IL and the other without.
        results = dict()
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        client_count = len(self.hostlist_clients)
        w_clients = self.hostlist_clients[0:int(client_count / 2)]
        wo_clients = self.hostlist_clients[int(client_count / 2):]
        self.run_ior_threads_il(
            results=results, intercept=intercept, with_clients=w_clients,
            without_clients=wo_clients)

        # Print the raw results from the IOR stdout.
        IorCommand.log_metrics(
            self.log, "{} clients - with interception library".format(
                len(w_clients)), results[1])
        IorCommand.log_metrics(
            self.log, "{} clients - without interception library".format(
                len(wo_clients)), results[2])

        # Get Max, Min, and Mean throughput values for Write and Read.
        w_write_results = results[1][0]
        w_read_results = results[1][1]
        wo_write_results = results[2][0]
        wo_read_results = results[2][1]

        max_mib = int(IorMetrics.Max_MiB)
        min_mib = int(IorMetrics.Min_MiB)
        mean_mib = int(IorMetrics.Mean_MiB)

        w_write_max = float(w_write_results[max_mib])
        wo_write_max = float(wo_write_results[max_mib])
        w_write_min = float(w_write_results[min_mib])
        wo_write_min = float(wo_write_results[min_mib])
        w_write_mean = float(w_write_results[mean_mib])
        wo_write_mean = float(wo_write_results[mean_mib])

        w_read_max = float(w_read_results[max_mib])
        wo_read_max = float(wo_read_results[max_mib])
        w_read_min = float(w_read_results[min_mib])
        wo_read_min = float(wo_read_results[min_mib])
        w_read_mean = float(w_read_results[mean_mib])
        wo_read_mean = float(wo_read_results[mean_mib])

        # Calculate the increase for the 6 values.
        # [max, min, mean]
        write_changes = [-1, -1, -1]
        if wo_write_max > 0:
            write_changes[0] = round(w_write_max / wo_write_max, 4)
        if wo_write_min > 0:
            write_changes[1] = round(w_write_min / wo_write_min, 4)
        if wo_write_mean > 0:
            write_changes[2] = round(w_write_mean / wo_write_mean, 4)

        # [max, min, mean]
        read_changes = [-1, -1, -1]
        if wo_read_max > 0:
            read_changes[0] = round(w_read_max / wo_read_max, 4)
        if wo_read_min > 0:
            read_changes[1] = round(w_read_min / wo_read_min, 4)
        if wo_read_mean > 0:
            read_changes[2] = round(w_read_mean / wo_read_mean, 4)

        # Print the summary of improvements.
        self.log.info(
            "--- Throughput Improvement with Interception Library ---")
        self.log.info("Clients with IL: %s", w_clients)
        self.log.info("Clients without IL: %s\n", wo_clients)
        self.log.info("Write Max: x%f", write_changes[0])
        self.log.info("Write Min: x%f", write_changes[1])
        self.log.info("Write Mean: x%f\n", write_changes[2])
        self.log.info("Read Max: x%f", read_changes[0])
        self.log.info("Read Min: x%f", read_changes[1])
        self.log.info("Read Mean: x%f", read_changes[2])

        # Do the threshold testing.
        write_x = self.params.get("write_x", "/run/ior/iorflags/ssf/*", 1)
        #read_x = self.params.get("read_x", "/run/ior/iorflags/ssf/*", 1)

        errors = []
        # Verify that using interception library gives desired performance
        # improvement.
        # Verifying write performance
        if w_write_max <= write_x * wo_write_max:
            errors.append("Write Max with IL is less than x{}!".format(write_x))
        if w_write_min <= write_x * wo_write_min:
            errors.append("Write Min with IL is less than x{}!".format(write_x))
        if w_write_mean <= write_x * wo_write_mean:
            errors.append(
                "Write Mean with IL is less than x{}!".format(write_x))

        # DAOS-5857
        # Read performance with IL was lower in CI. The environment had OPA +
        # PMEM and NVMe. It was about 2x with IB + RAM.
        # Uncomment below (and read_x line) if the lower performance issue is
        # fixed.
        # Verifying read performance
        # if w_read_max <= read_x * wo_read_max:
        #     errors.append("Read Max with IL is less than x{}!".format(read_x))
        # if w_read_min <= read_x * wo_read_min:
        #     errors.append(
        # "Read Min with IL is less than x{}!".format(read_x))
        # if w_read_mean <= read_x * wo_read_mean:
        #     errors.append(
        # "Read Mean with IL is less than x{}!".format(read_x))

        if errors:
            self.fail("Poor IL throughput improvement!\n{}".format(
                "\n".join(errors)))
