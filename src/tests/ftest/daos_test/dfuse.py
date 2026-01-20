"""
  (C) Copyright 2021-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from collections import OrderedDict

from apricot import TestWithServers
from cmocka_utils import CmockaUtils, get_cmocka_command
from dfuse_utils import get_dfuse, start_dfuse
from file_utils import create_directory
from general_utils import get_log_file
from job_manager_utils import get_job_manager


class DaosCoreTestDfuse(TestWithServers):
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

        # Create a pool, container and start dfuse.
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

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
            cont_attrs['dfuse-data-cache'] = 'otoc'
            cont_attrs['dfuse-attr-time'] = cache_time
            cont_attrs['dfuse-dentry-time'] = cache_time
            cont_attrs['dfuse-ndentry-time'] = cache_time
        elif cache_mode == 'otoc':
            cont_attrs['dfuse-data-cache'] = 'otoc'
            cont_attrs['dfuse-attr-time'] = '0'
            cont_attrs['dfuse-dentry-time'] = '0'
            cont_attrs['dfuse-ndentry-time'] = '0'
        elif cache_mode == 'native':
            use_dfuse = False
        else:
            self.fail(f'Invalid cache_mode: {cache_mode}')

        if use_dfuse:
            container.set_attr(attrs=cont_attrs)

            dfuse = get_dfuse(self, self.hostlist_clients)
            start_dfuse(self, dfuse, pool, container)

            mount_dir = dfuse.mount_dir.value
        else:
            # Bypass, simply create a remote directory and use that.
            mount_dir = '/tmp/dfuse-test'
            result = create_directory(self.log, self.hostlist_clients, mount_dir)
            if not result.passed:
                self.fail(f"Error creating {mount_dir} on {result.failed_hosts}")

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

            if il_lib == 'libpil4dfs.so':
                daos_test_env['D_IL_MOUNT_POINT'] = mount_dir
                daos_test_env['D_IL_POOL'] = pool.identifier
                daos_test_env['D_IL_CONTAINER'] = container.identifier
                daos_test_env['D_IL_REPORT'] = '0'
                daos_test_env['D_IL_MAX_EQ'] = '2'
                daos_test_env['D_IL_NO_BYPASS'] = '1'

        command = os.path.join(self.bin, 'dfuse_test')
        parameters = [
            '--test-dir',
            mount_dir,
            '--io',
            '--stream',
            '--mmap',
            '--exec',
            '--directory',
            '--cache'
        ]
        if use_dfuse:
            parameters.append('--lowfd')
            parameters.append('--flock')
        else:
            # make D_IL_MOUNT_POINT different from mount_dir so it tests a non-DAOS filesystem
            dummy_dir = '/tmp/dummy'
            result = create_directory(self.log, self.hostlist_clients, dummy_dir)
            if not result.passed:
                self.fail(f"Error creating {dummy_dir} on {result.failed_hosts}")
            daos_test_env['D_IL_MOUNT_POINT'] = dummy_dir
        if cache_mode != 'writeback':
            parameters.append('--metadata')

        job = get_job_manager(self, "Clush", get_cmocka_command(command, ' '.join(parameters)))
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
