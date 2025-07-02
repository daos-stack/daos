"""
  (C) Copyright 2020-2022 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from ior_test_base import IorTestBase
from run_utils import run_remote


class CPUUsage(IorTestBase):
    """Test Class Description:
    Start daos_engine and measure CPU usage of daos_engine with target = 16,
    nr_xs_helpers = 16 and verify that it's less than 200%. Run IOR and verify
    that it goes down.

    This test uses "top" command. It aggregates the CPU usage of every core.
    e.g., If engine is using 50% per core for 8 cores, top shows 400%.

    :avocado: recursive
    """

    def get_cpu_usage(self, pid, usage_limit):
        """Monitor CPU usage and return if it gets below usage_limit.

        Args:
            pid (str): daos_engine PID.
            usage_limit (int): Limit that we want daos_engine to use.

        Returns:
            str: daos_engine CPU usage.
        """
        usage = -1
        for _ in range(10):
            # Get (instantaneous) CPU usage of the PID with top.
            top_pid = "top -p {} -b -n 1".format(pid)
            usage = -1
            result = run_remote(self.log, self.hostlist_servers, top_pid)
            if not result.passed:
                self.fail(f"{top_pid} failed on {result.failed_hosts}")
            for data in result.output:
                process_row = data.stdout[-1]
                self.log.info("Process row = %s", process_row)
                values = process_row.split()
                self.log.info("Values = %s", values)
                if len(values) < 9:
                    self.fail("{} returned invalid output!".format(top_pid))
                usage = values[8]
                self.log.info("CPU Usage = %s", usage)
            if usage != -1 and float(usage) < usage_limit:
                break
            time.sleep(2)
        return usage

    def verify_usage(self, usage, usage_limit):
        """Verify CPU usage.

        Args:
            usage (str): daos_engine CPU usage.
            usage_limit (int): Limit that we want daos_engine to use.
        """
        self.assertTrue(usage != -1, "daos_engine CPU usage couldn't be obtained!")
        self.assertTrue(
            float(usage) < usage_limit, "CPU usage is above {}%: {}%".format(usage, usage_limit))

    def test_cpu_usage(self):
        """JIRA ID: DAOS-4826.

        Test Description: Test CPU usage of formatted and idle engine.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=server
        :avocado: tags=CPUUsage,test_cpu_usage
        """
        # Get PID of daos_engine with ps.
        ps_engine = r"ps -C daos_engine -o %\p"
        pid_found = False
        # At this point, daos_engine should be started, but do the repetitive
        # calls just in case.
        for _ in range(5):
            result = run_remote(self.log, self.hostlist_servers, ps_engine)
            if not result.passed:
                self.fail(f"{ps_engine} failed on {result.failed_hosts}")
            for data in result.output:
                pid = data.stdout[-1]
                self.log.info("PID = %s", pid)
                if "PID" not in pid:
                    pid_found = True
            if pid_found:
                break
            time.sleep(5)
        if not pid_found:
            self.fail("daos_engine PID couldn't be obtained!")

        # Get and verify CPU usage.
        usage_limit = self.params.get("usage_limit", '/run/*')
        usage = self.get_cpu_usage(pid=pid, usage_limit=usage_limit)
        self.verify_usage(usage=usage, usage_limit=usage_limit)

        # Create a pool, container, and run IOR. IO will invoke CPU usage by
        # daos_engine.
        self.run_ior_with_pool()

        # Verify that the CPU usage goes down after IO.
        usage = self.get_cpu_usage(pid=pid, usage_limit=usage_limit)
        self.verify_usage(usage=usage, usage_limit=usage_limit)
