"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithoutServers

from host_utils import get_local_host
from slurm_utils import sinfo


class HarnessSlurmTest(TestWithoutServers):
    """Harness slurm test cases.

    :avocado: recursive
    """

    def test_partition(self):
        """Verify that launch.py correctly creates the slurm partition defined by this test.

        :avocado: tags=all
        :avocado: tags=hw,small,medium,large
        :avocado: tags=harness,harness_slurm_test,test_partition
        """
        control = get_local_host()
        result = sinfo(self.log, control)
        if not result.passed:
            self.fail('Error running sinfo')
        if 'daos_client_partition' not in '\n'.join(result.all_stdout.values()):
            self.fail('Error partition not found in sinfo output')
        self.log.info('Test passed!')
