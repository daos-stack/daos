"""
  (C) Copyright 2021-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from collections import OrderedDict
import general_utils
from dfuse_test_base import DfuseTestBase

class DaosCoreTestDfuse(DfuseTestBase):
    # pylint: disable=too-many-ancestors
    """Runs DAOS DFuse tests.

    :avocado: recursive
    """

    def test_daos_dfuse_unit(self):
        """

        Test Description: Run dfuse_test to check correctness.

        Use cases:
            DAOS DFuse unit tests

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_test
        :avocado: tags=dfuse_unit,test_daos_dfuse_unit
        """
        self.daos_test = os.path.join(self.bin, 'dfuse_test')

        # Create a pool, container and start dfuse.
        self.add_pool(connect=False)
        self.add_container(self.pool)

        daos_cmd = self.get_daos_command()

        cont_attrs = OrderedDict()

        cache_mode = self.params.get('name', '/run/dfuse/*')

        # How long to cache things for, if caching is enabled.
        cache_time = '5m'

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
        else:
            self.fail('Invalid cache_mode: {}'.format(cache_mode))

        for key, value in cont_attrs.items():
            daos_cmd.container_set_attr(pool=self.pool.uuid, cont=self.container.uuid,
                                        attr=key, val=value)

        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        mount_dir = self.dfuse.mount_dir.value

        intercept = self.params.get('use_intercept', '/run/intercept/*', default=False)

        cmd = [self.daos_test, '--test-dir', mount_dir]

        if intercept:
            remote_env = OrderedDict()

            remote_env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libioil.so')
            remote_env['D_LOG_FILE'] = '/var/tmp/daos_testing/daos-il.log'
            remote_env['DD_MASK'] = 'all'
            remote_env['DD_SUBSYS'] = 'all'
            remote_env['D_LOG_MASK'] = 'INFO,IL=DEBUG'

            envs = ['export {}={}'.format(env, value) for env, value in remote_env.items()]

            preload_cmd = ';'.join(envs)

            command = '{};{}'.format(preload_cmd, ' '.join(cmd))
        else:
            command = ' '.join(cmd)
        ret_code = general_utils.pcmd(self.hostlist_clients, command, timeout=60)
        if 0 in ret_code:
            return
        self.log.info(ret_code)
        self.fail('Error running {}'.format(cmd))
