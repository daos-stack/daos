#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
from ior_test_base import IorTestBase
from general_utils import get_log_file, run_pcmd

# List of NVMe statistics
NVME_STATS = ['read_bytes',
              'read_ops',
              'write_bytes',
              'write_ops',
              'read_latency_ticks',
              'write_latency_ticks']


class NvmeIOStates(IorTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Test class Description: Runs IOR with 1 server with basic parameters and
       verify NVMe IO statistics parameters getting increase.

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
        # run ior
        self.run_ior_with_pool()

        # Get the NVMe IO statistics from server control_log file.
        cmd = 'cat {}'.format(get_log_file(self.control_log))
        results = run_pcmd(self.hostlist_servers, cmd)
        for result in results:
            if result["exit_status"] == 1:
                self.fail(
                    "Failed to run cmd {} on {}".format(cmd, result["hosts"]))

            # Verify statistics are increasing for IO
            target_stats = []
            for _tmp in range(8):
                target_stats.append(
                    [line for line in result["stdout"]
                     if "tgt[{}]".format(_tmp) in line])
            for stats in NVME_STATS:
                for _tgt in range(len(target_stats)):
                    first_stats = re.findall(
                        r'\d+',
                        [x for x in target_stats[_tgt][0].split()
                         if re.search(stats, x)][0])[0]
                    last_stats = re.findall(
                        r'\d+',
                        [x for x in target_stats[_tgt][-1].split()
                         if re.search(stats, x)][0])[0]
                    # Last statistic should be higher than initial statistics
                    if int(first_stats) >= int(last_stats):
                        self.fail(
                            "Failed: Stats {} for target {} did not increased "
                            "First_stat={} < Last_stat={}".format(
                                stats, _tgt, first_stats, last_stats))
