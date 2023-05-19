"""
  (C) Copyright 2021-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from collections import OrderedDict

from cmocka_utils import CmockaUtils
from dfuse_test_base import DfuseTestBase
from general_utils import create_directory, get_log_file
from job_manager_utils import get_job_manager


class DaosCoreTestDfuse(DfuseTestBase):
    """Runs DAOS DFuse tests.

    :avocado: recursive
    """

    def run_test(self, il_lib=None):
        """
        Test Description: Run dfuse_test to check correctness.

        Use cases:
            DAOS DFuse unit tests with an interception library
        """
        if il_lib is None:
            self.fail('il_lib is not defined.')

        self.daos_test = os.path.join(self.bin, 'dfuse_test')

        # Create a pool, container and start dfuse.
        self.add_pool(connect=False)
        self.add_container(self.pool)

        cont_attrs = OrderedDict()

        cache_mode = self.params.get('name', '/run/dfuse/*')

        # How long to cache things for, if caching is enabled.
        cache_time = '5m'

        use_dfuse = True

        if cache_mode == 'writeback':
            cont_attrs['dfuse-data-cache'] = 'on'
            cont_attrs['dfuse-attr-time'] = cache_time
            cont_attrs['dfuse-dentry-time'] = cache_time
            cont_attrs['dfuse-ndentry-time'] = cache_time
        elif cache_mode == 'writethrough':
            cont_attrs['dfuse-data-cache'] = 'on'
            cont_attrs['dfuse-attr-time'] = cache_time
            cont_attrs['dfuse-dentry-time'] = cache_time
            cont_attrs['dfuse-ndentry-time'] = cache_time
        elif cache_mode == 'metadata':
            cont_attrs['dfuse-data-cache'] = 'off'
            cont_attrs['dfuse-attr-time'] = cache_time
            cont_attrs['dfuse-dentry-time'] = cache_time
            cont_attrs['dfuse-ndentry-time'] = cache_time
        elif cache_mode == 'nocache':
            cont_attrs['dfuse-data-cache'] = 'off'
            cont_attrs['dfuse-attr-time'] = '0'
            cont_attrs['dfuse-dentry-time'] = '0'
            cont_attrs['dfuse-ndentry-time'] = '0'
        elif cache_mode == 'native':
            use_dfuse = False
        else:
            self.fail('Invalid cache_mode: {}'.format(cache_mode))

        if use_dfuse:
            self.container.set_attr(attrs=cont_attrs)

            self.start_dfuse(self.hostlist_clients, self.pool, self.container)

            mount_dir = self.dfuse.mount_dir.value
        else:
            # Bypass, simply create a remote directory and use that.
            mount_dir = '/tmp/dfuse-test'
            create_directory(self.hostlist_clients, mount_dir)

        cmocka_utils = CmockaUtils(
            self.hostlist_clients, "dfuse", self.outputdir, self.test_dir, self.log)
        daos_test_env = cmocka_utils.get_cmocka_env()
        intercept = self.params.get('use_intercept', '/run/intercept/*', default=False)
        if intercept:
            daos_test_env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', il_lib)
            daos_test_env['D_LOG_FILE'] = get_log_file(
                'daos-' + il_lib.replace(".so", "").replace("lib", "") + '.log')
            daos_test_env['DD_MASK'] = 'all'
            daos_test_env['DD_SUBSYS'] = 'all'
            daos_test_env['D_LOG_MASK'] = 'INFO,IL=DEBUG'

        command = [self.daos_test, '--test-dir', mount_dir, '--io', '--stream']
        if cache_mode != 'writeback':
            command.append('--metadata')

        job = get_job_manager(self, "Clush", cmocka_utils.get_cmocka_command(" ".join(command)))
        job.assign_hosts(cmocka_utils.hosts)
        job.assign_environment(daos_test_env)

        cmocka_utils.run_cmocka_test(self, job)
        if not job.result.passed:
            self.fail(f'Error running {job.command} on {job.hosts}')

    def test_daos_dfuse_unit_ioil(self):
        """
        Test Description: Run dfuse_test to check correctness.

        Use cases:
            DAOS DFuse unit tests with an interception library

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_test,daos_cmd
        :avocado: tags=DaosCoreTestDfuse,dfuse_unit,test_daos_dfuse_unit_ioil
        """
        self.run_test(il_lib='libioil.so')

    def test_daos_dfuse_unit_pil4dfs(self):
        """
        Test Description: Run dfuse_test to check correctness.

        Use cases:
            DAOS DFuse unit tests with an interception library

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_test,daos_cmd,pil4dfs
        :avocado: tags=DaosCoreTestDfuse,dfuse_unit,test_daos_dfuse_unit_pil4dfs
        """
        self.run_test(il_lib='libpil4dfs.so')
