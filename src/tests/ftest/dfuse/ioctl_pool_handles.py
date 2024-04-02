"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from dfuse_utils import get_dfuse, start_dfuse
from ior_utils import write_data
from telemetry_test_base import TestWithTelemetry


class IoctlPoolHandles(TestWithTelemetry):
    """Verifies interception library is using the same pool/container handles as dfuse."""

    def test_ioctl_pool_handles(self):
        """JIRA ID: DAOS-8403.

        Test Description:
            Verify that the interception library is using the same pool/container handles as dfuse.

            Steps:
                1.) Start 4 Server ranks with 16 targets each. Start agent on one of the nodes.
                2.) Create Pool and posix type container.
                3.) Mount dfuse.
                4.) Get initial engine_pool_ops_cont_open and engine_pool_ops_pool_connect telemetry
                    metrics values
                5.) Set D_IL_REPORT=2 and LD_PRELOAD=/usr/lib64/libioil.so to use IL
                6.) Run ior
                7.) Check pool/cont handle count telemetry metrics after ior completion. The values
                    should match the initial values.
                8.) Unmount and mount dfuse again.
                9.) Check pool/cont handle count telemetry metrics again. The values should have
                    increased over the initial values.

        Note: As this test previously required a modification to src/client/dfuse/il/int_posix.c
            (https://github.com/daos-stack/daos/pull/9941) to run and pass, but now passes w/o it.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=IoctlPoolHandles,test_ioctl_pool_handles
        """
        metrics = ['engine_pool_ops_cont_open', 'engine_pool_ops_pool_connect']

        self.log_step('Creating a 10GB pool')
        pool = self.get_pool()

        self.log_step('Creating a POSIX container')
        container = self.get_container(pool)

        self.log_step('Starting/mounting dfuse')
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        self.log_step('Collecting container metrics before running ior')
        initial = self.telemetry.collect_data(metrics)
        self.telemetry.display_data()
        for metric in list(initial):
            for label in initial[metric]:
                value = initial[metric][label]
                initial[metric][label] = [value, value]

        self.log_step('Run ior with intercept library')
        ior_params = {'dfuse': dfuse, 'intercept': '/usr/lib64/libioil.so', 'il_report': 2}
        write_data(self, container, **ior_params)

        self.log_step('Collecting pool/container metrics after running ior')
        after_ior = self.telemetry.collect_data(metrics)
        for metric in list(after_ior):
            for label in after_ior[metric]:
                value = after_ior[metric][label]
                if value > 0:
                    value += 1
                after_ior[metric][label] = [value]

        self.log_step('Verifying pool/container handle ops have not increased after running ior')
        if not self.telemetry.verify_data(initial):
            self.fail('Pool/container handle ops increased after running ior')

        self.log_step('Unmount dfuse')
        dfuse.unmount()

        self.log_step('Remount dfuse')
        dfuse.run()

        self.log_step('Collecting pool/container metrics after remounting dfuse')
        self.telemetry.collect_data(metrics)

        self.log_step('Verifying pool/container handle ops have increased after remounting dfuse')
        if not self.telemetry.verify_data(after_ior):
            self.fail('Pool/container handle ops did not increase after remounting dfuse')

        self.log.info('Test passed')
