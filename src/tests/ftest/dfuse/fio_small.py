"""
  (C) Copyright 2019-2023 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from dfuse_utils import get_dfuse, start_dfuse
from fio_test_base import FioBase


class FioSmall(FioBase):
    """Test class Description: Runs Fio with in small config.

    :avocado: recursive
    """

    def test_fio_small(self):
        """Jira ID: DAOS-2493.

        Test Description:
            Verify Fio in various small configs.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dfuse,fio,checksum,tx
        :avocado: tags=FioSmall,test_fio_small
        """
        self.log_step('Create a pool')
        pool = self.get_pool(connect=False)

        # Run with various container properties
        for properties in self.params.get("properties_variants", '/run/container/*'):
            self.log_step('Create a container with properties: %s', properties)
            container = self.get_container(pool, properties=properties)
            container.set_attr(attrs={'dfuse-direct-io-disable': 'on'})

            self.log_step('Start dfuse')
            dfuse = get_dfuse(self, self.hostlist_clients)
            start_dfuse(self, dfuse, pool, container)
            self.fio_cmd.update_directory(dfuse.mount_dir.value)

            # # Run with various fio parameters
            # for variant in self.params.get("variants", '/run/fio/global/*'):
            #     self.log_step(
            #         f'Run fio with direct={variant[0]}, blocksize={variant[1]}, '
            #         f'size={variant[2]}, rw={variant[3]}')
            #     self.fio_cmd.update('global', 'direct', variant[0], 'global.direct')
            #     self.fio_cmd.update('global', 'blocksize', variant[1], 'global.blocksize')
            #     self.fio_cmd.update('global', 'size', variant[2], 'global.size')
            #     self.fio_cmd.update('global', 'rw', variant[3], 'global.rw')
            #     self.execute_fio()

            self.log_step('Run fio')
            self.execute_fio()

            self.log_step('Stop dfuse and destroy container')
            dfuse.stop()
            container.destroy()
