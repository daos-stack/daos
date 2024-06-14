"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from dfuse_utils import get_dfuse, start_dfuse


class DfuseContainerCheck(TestWithServers):
    """Base Dfuse Container check test class.

    :avocado: recursive
    """

    def test_dfuse_container_check(self):
        """Jira ID: DAOS-3635.

        Test Description:
            Purpose of this test is to try and mount different container types
            to dfuse and check the behavior.
        Use cases:
            Create pool
            Create container of type default
            Try to mount to dfuse and check the behavior.
            Create container of type POSIX.
            Try to mount to dfuse and check the behavior.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=DfuseContainerCheck,test_dfuse_container_check
        """
        # get test params for cont and pool count
        cont_types = self.params.get('cont_types', '/run/container/*')

        # Create a pool and start dfuse.
        self.log_step('Creating a single pool')
        pool = self.get_pool(connect=False)

        for cont_type in cont_types:
            description = f"{cont_type if cont_type == 'POSIX' else 'non-POSIX'}"
            # Get container params
            self.log_step(f'Creating a {description} container')
            container = self.get_container(pool, create=False)
            # create container
            if cont_type == 'POSIX':
                container.type.update(cont_type)
            container.create()

            # Attempt to mount the dfuse mount point - this should only succeed
            # with a POSIX container
            self.log_step(f'Attempting to mount dfuse with a {description} container')
            dfuse = get_dfuse(self, self.hostlist_clients)
            try:
                start_dfuse(self, dfuse, pool, container)
                if cont_type != 'POSIX':
                    self.fail(f'Dfuse mount succeeded with a {description} container')

            except TestFail as error:
                if cont_type == 'POSIX':
                    self.log.debug('%s', error)
                    self.fail(f'Dfuse failed to mount with a {description} container')
                self.log.info('Dfuse mount expected to fail with a %s container', description)

            # Verify dfuse is running on the POSIX type container, then stop dfuse
            if cont_type == 'POSIX':
                self.log_step(f'Verifying dfuse is running with a {description} container')
                dfuse.check_running()
                self.log_step(f'Stopping dfuse with a {description} container')
                dfuse.stop()

            # Destroy the container for next iteration
            self.log_step(f'Destroying a {description} container')
            container.destroy(1)

        self.log.info('Test passed')
