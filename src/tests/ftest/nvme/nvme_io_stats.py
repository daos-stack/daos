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
import re
from ior_test_base import IorTestBase
from general_utils import get_log_file, run_task

#List of NVMe statistics
NVME_STATS = ['read_bytes',
              'read_ops',
              'write_bytes',
              'write_ops',
              'read_latency_ticks',
              'write_latency_ticks']

class NvmeIOStates(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR with 1 server with basic parameters and
       verify NVMe IO statistics paramters getting increase.

    :avocado: recursive
    """

    def test_nvme_io_stats(self):
        """Jira ID: DAOS-4722.

        Test Description:
            Purpose of this test is to run IO test and check when NVME_IO_STATS
            enabled in config, it generates the different statistics.

        Use case:
            Run ior and it will print the NVMe IO stats to control plane log
            file.

        :avocado: tags=all,hw,medium,nvme,ib2,nvme_io_stats,full_regression
        """
        transfer_block_size = self.params.get("transfer_block_size",
                                              '/run/ior/iorflags/*')

        # Update IOR parameter
        self.ior_cmd.flags.update(self.params.get("ior_flags",
                                                  '/run/ior/iorflags/*'))
        self.ior_cmd.daos_oclass.update(self.params.get("obj_class",
                                                        '/run/ior/iorflags/*'))
        self.ior_cmd.api.update(self.params.get("ior_api",
                                                '/run/ior/iorflags/*'))
        self.ior_cmd.transfer_size.update(transfer_block_size[0])
        self.ior_cmd.block_size.update(transfer_block_size[1])
        # run ior
        self.run_ior_with_pool()

        #Get the NVMe IO statistics from server control_log file.
        cmd = 'cat {}'.format(get_log_file(self.control_log))
        task = run_task(self.hostlist_servers, cmd)
        for _rc_code, _node in task.iter_retcodes():
            if _rc_code == 1:
                self.fail("Failed to run cmd {} on {}".format(cmd, _node))
        for buf, _nodes in task.iter_buffers():
            output_list = str(buf).split('\n')

        #Verify statistics are increasing for IO
        target_stats = []
        for _tmp in range(8):
            target_stats.append([s for s in output_list if "tgt[{}]"
                                 .format(_tmp) in s])
        for stats in NVME_STATS:
            for _tgt in range(len(target_stats)):
                first_stats = re.findall(
                    r'\d+', [x for x in target_stats[_tgt][0].split()
                             if re.search(stats, x)][0])[0]
                last_stats = re.findall(
                    r'\d+', [x for x in  target_stats[_tgt][-1].split()
                             if re.search(stats, x)][0])[0]
                #Last statistic should be higher from the initial statistics
                if int(first_stats) >= int(last_stats):
                    self.fail('Failed: Stats {} for target {} did not increased'
                              ' First_stat={} < Last_stat={}'
                              .format(stats, _tgt, first_stats, last_stats))
