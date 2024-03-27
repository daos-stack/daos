"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from dfuse_utils import get_dfuse, start_dfuse
from ior_utils import run_ior
from telemetry_test_base import TestWithTelemetry


class IoctlPoolHandles(TestWithTelemetry):
    """Verifies multi-user dfuse mounting"""

    def verify_metrics(self, previous, current, expect_increase):
        """Verify previous and current metric data matches or the current data values are greater.

        Args:
            previous (dict): previous metric data to compare
            current (dict): current metric data to compare
            expect_increase (bool): if True current values should be greater or equal than the
                previous values; otherwise they should be equal.

        Returns:
            bool: True if the previous and current metric data sets match as expected
        """
        status = bool(previous)
        for host, data in previous.items():
            for name, info in data.items():
                try:
                    if expect_increase:
                        if info['metrics']['value'] < current[host][name]['metrics']['value']:
                            status |= False
                            self.log.error(
                                "%s on %s previous value (%s) < current value (%s)",
                                name, host, info['metrics']['value'],
                                current[host][name]['metrics']['value'])
                    else:
                        if info['metrics']['value'] != current[host][name]['metrics']['value']:
                            status |= False
                            self.log.error(
                                "%s on %s previous value (%s) != current value (%s)",
                                name, host, info['metrics']['value'],
                                current[host][name]['metrics']['value'])
                except KeyError as error:
                    status |= False
                    self.log.error('Invalid inputs for verify_metrics(): %s', str(error))
        return status

    def test_ioctl_pool_handles(self):
        """JIRA ID: DAOS-8403.

        Test Description:
            Verify that the interception library is using the same pool/container handles as dfuse.

            Steps:
                0.) Manually impose this patch on top of the daos release under test:
                    https://github.com/daos-stack/daos/pull/9941
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

        Note: As this test currently requires a modification to src/client/dfuse/il/int_posix.c to
            run and pass, it should not be run in normal CI, but in a PR that includes the
            modification.

        :avocado: tags=all
        :avocado: tags=hw,medium
        :avocado: tags=dfuse,
        :avocado: tags=IoctlPoolHandles,test_ioctl_pool_handles
        """
        processes = self.params.get("np", '/run/ior/*')
        metrics = ['engine_pool_ops_cont_open', 'engine_pool_ops_pool_connect']

        self.log_step('Creating a 10GB pool')
        pool = self.get_pool()

        self.log_step('Creating a POSIX container')
        container = self.get_container(pool)

        self.log_step('Starting/mounting dfuse')
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        self.log_step('Collecting container metrics before running ior')
        initial = self.telemetry.get_metrics(metrics)

        self.log_step('Run ior with intercept library')
        run_ior(
            self, self.job_manager, 'ior.log', self.hostlist_clients, self.workdir, None,
            self.server_group, pool, container, processes, None, '/usr/lib64/libioil.so', None,
            dfuse, True, False, '/run/ior/*', {'env': {'D_IL_REPORT': 2}})

        self.log_step('Collecting pool/container metrics after running ior')
        compare = self.telemetry.get_metrics(metrics)

        self.log_step('Verifying pool/container handle ops have not increased after running ior')
        self.verify_metrics(initial, compare, False)

        self.log_step('Unmount dfuse')
        dfuse.unmount()

        self.log_step('Remount dfuse')
        dfuse.run()

        self.log_step('Collecting pool/container metrics after remounting dfuse')
        compare = self.telemetry.get_metrics(metrics)

        self.log_step('Verifying pool/container handle ops have increased after remounting dfuse')
        self.verify_metrics(initial, compare, True)

        self.log.info('Test passed')
