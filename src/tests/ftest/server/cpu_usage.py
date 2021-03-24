#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from general_utils import run_pcmd


class CPUUsage(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Test Class Description:
    Measure CPU usage of daos_engine with target = 16 and verify that it's
    less than 100%.

    This test uses "top" command. It aggregates the CPU usage of every core.
    e.g., If engine is using 50% per core for 8 cores, top shows 400%.
    :avocado: recursive
    """

    def test_cpu_usage(self):
        """
        JIRA ID: DAOS-4826

        Test Description: Test CPU usage of formatted and idle engine.

        :avocado: tags=all,full_regression
        :avocado: tags=server
        :avocado: tags=cpu_usage
        """
        # Get PID of daos_engine with ps.
        ps_engine = r"ps -C daos_engine -o %\p"
        pid_found = False
        # At this point, daos_engine should be started, but do the repetitive
        # calls just in case.
        for _ in range(5):
            results = run_pcmd(hosts=self.hostlist_servers, command=ps_engine)
            for result in results:
                self.log.info("ps output = %s", "\n".join(result["stdout"]))
                pid = result["stdout"][-1]
                self.log.info("PID = %s", pid)
                if "PID" not in pid:
                    pid_found = True
            if pid_found:
                break
            time.sleep(5)
        if not pid_found:
            self.fail("daos_engine PID couldn't be obtained!")

        for _ in range(10):
            # Get (instantaneous) CPU usage of the PID with top.
            top_pid = "top -p {} -b -n 1".format(pid)
            usage = -1
            results = run_pcmd(hosts=self.hostlist_servers, command=top_pid)
            for result in results:
                process_row = result["stdout"][-1]
                self.log.info("Process row = %s", process_row)
                values = process_row.split()
                self.log.info("Values = %s", values)
                if len(values) < 9:
                    self.fail("{} returned invalid output!".format(top_pid))
                usage = values[8]
                self.log.info("CPU Usage = %s", usage)
            if usage != -1 and float(usage) < 100:
                break
            time.sleep(2)

        self.assertTrue(
            usage != -1, "daos_engine CPU usage couldn't be obtained!")
        self.assertTrue(
            float(usage) < 100, "CPU usage is above 100%: {}%".format(usage))
