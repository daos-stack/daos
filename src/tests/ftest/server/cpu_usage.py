#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
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
        """
        JIRA ID: DAOS-4826
        Test Description: Test CPU usage of daos_server.
        :avocado: tags=all,server,small,full_regression,cpu_usage
        """
        ps_get_cpu = "ps -C daos_io_server -o %\cpu"
        # It takes about 5 sec for CPU usage to become stable.
        time.sleep(10)
        task = run_task(hosts=self.hostlist_servers, command=ps_get_cpu)
        # Sample output.
        # %CPU
        # 1798
        for output, nodes in task.iter_buffers():
            usage = str(output).splitlines()[-1]
            self.log.info("CPU usage = %s", usage)
            self.assertTrue(
				int(usage) < 100, "CPU usage is above 100%: {}%".format(usage))
