#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers, skipForTicket
from general_utils import run_task
import time


class CPUUsage(TestWithServers):
    """Test Class Description:
    Measure CPU usage of daos_server with target = 16 and verify that it's
    less than 100%.
    :avocado: recursive
    """

    @skipForTicket("DAOS-5504")
    def test_cpu_usage(self):
        # pylint: disable=pylint-bad-continuation
        """
        JIRA ID: DAOS-4826
        Test Description: Test CPU usage of formatted and idle daos_io_server.
        :avocado: tags=all,hw,server,small,full_regression,cpu_usage
        """
        ps_get_cpu = r"ps -C daos_io_server -o %\cpu"

        prev_usage = 1
        usage = 1
        time.sleep(5)
        for _ in range(10):
            time.sleep(5)
            task = run_task(hosts=self.hostlist_servers, command=ps_get_cpu)
            # Sample output.
            # %CPU
            # 1798
            for output, _ in task.iter_buffers():
                usage = str(output).splitlines()[-1]
                self.log.info("CPU usage = %s", usage)
            # Check if daos_io_server has started.
            if usage == "%CPU":
                continue

            usage = int(usage)
            if usage == 0:
                break
            diff = usage - prev_usage
            diff_p = (float(abs(diff)) / prev_usage) * 100

            # Check if the CPU usage is stable; the change was less than 10%.
            if diff_p <= float(10):
                break
            prev_usage = usage

        self.assertTrue(
            usage != "%CPU", "daos_io_server CPU usage couldn't be obtained!")
        self.assertTrue(
            usage < 100, "CPU usage is above 100%: {}%".format(usage))
