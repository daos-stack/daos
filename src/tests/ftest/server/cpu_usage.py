#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from general_utils import run_task
import time


class CPUUsage(TestWithServers):
    """Test Class Description:
    Measure CPU usage of daos_server with target = 16 and verify that it's
    less than 100%.
    :avocado: recursive
    """

    def test_cpu_usage(self):
        # pylint: disable=pylint-bad-continuation
        """
        JIRA ID: DAOS-4826
        Test Description: Test CPU usage of formatted and idle engine.
        :avocado: tags=all,hw,server,small,full_regression,cpu_usage
        """
        # Get PID of daos_engine with ps.
        ps_engine = r"ps -C daos_engine -o %\p"
        pid_found = False
        # At this point, daos_engine should be started, but do the repetetive
        # calls just in case.
        for _ in range(5):
            task = run_task(hosts=self.hostlist_servers, command=ps_engine)
            for output, _ in task.iter_buffers():
                self.log.info("ps output = %s", output)
                pid = str(output).splitlines()[-1]
                self.log.info("PID = %s", pid)
                if "PID" not in pid:
                    pid_found = True
            if pid_found:
                break
            time.sleep(5)
        if not pid_found:
            self.fail("daos_engine PID couldn't be obtained!")

        # Wait for CPU usage to stabilize. It usually takes a few sec.
        time.sleep(10)

        # Get (instantenious) CPU usage of the PID with top.
        top_pid = "top -p {} -b -n 1".format(pid)
        usage = -1
        task = run_task(hosts=self.hostlist_servers, command=top_pid)
        for output, _ in task.iter_buffers():
            process_row = str(output).splitlines()[-1]
            self.log.info("Process row = %s", process_row)
            values = process_row.split()
            self.log.info("Values = %s", values)
            usage = values[8]
            self.log.info("CPU Usage = %f", usage)

        self.assertTrue(
            usage != -1, "daos_engine CPU usage couldn't be obtained!")
        self.assertTrue(
            float(usage) < 100, "CPU usage is above 100%: {}%".format(usage))
