"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import time
import general_utils

from dfuse_test_base import DfuseTestBase


class DaosBuild(DfuseTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Build DAOS over dfuse.

    :avocado: recursive
    """

    def test_dfuse_daos_build_wb(self):
        """This test builds DAOS on a dfuse filesystem.

        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,dfuse
        :avocado: tags=DaosBuild,test_dfuse_daos_build_wb
        """
        self.run_build_test("writeback")

    def test_dfuse_daos_build_wt(self):
        """This test builds DAOS on a dfuse filesystem.

        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,dfuse
        :avocado: tags=DaosBuild,test_dfuse_daos_build_wt
        """
        self.run_build_test("writethrough")

    def test_dfuse_daos_build_wt_il(self):
        """This test builds DAOS on a dfuse filesystem.

        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=daosio,dfuse
        :avocado: tags=DaosBuild,test_dfuse_daos_build_wt_il
        """
        self.run_build_test("writethrough", True, dfuse_namespace="/run/dfuse_vm/*")

    def test_dfuse_daos_build_metadata(self):
        """This test builds DAOS on a dfuse filesystem.

        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,dfuse
        :avocado: tags=DaosBuild,test_dfuse_daos_build_metadata
        """
        self.run_build_test("metadata")

    def test_dfuse_daos_build_data(self):
        """This test builds DAOS on a dfuse filesystem.

        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,dfuse
        :avocado: tags=DaosBuild,test_dfuse_daos_build_data
        """
        self.run_build_test("data")

    def test_dfuse_daos_build_nocache(self):
        """This test builds DAOS on a dfuse filesystem.

        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,dfuse
        :avocado: tags=DaosBuild,test_dfuse_daos_build_nocache
        """
        self.run_build_test("nocache")

    def run_build_test(self, cache_mode, intercept=False, dfuse_namespace=None):
        """"Run an actual test from above."""

        # Create a pool, container and start dfuse.
        self.add_pool(connect=False)
        self.add_container(self.pool)

        daos_cmd = self.get_daos_command()

        cont_attrs = {}

        # How long to cache things for, if caching is enabled.
        cache_time = '60m'
        # Timeout.  This is per command so up to double this or more as there are two scons
        # commands which can both take a long time.
        build_time = 10

        self.load_dfuse(self.hostlist_clients, dfuse_namespace)

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
            if intercept:
                build_time = 360
            self.dfuse.disable_wb_cache.value = True
        elif cache_mode == 'metadata':
            cont_attrs['dfuse-data-cache'] = 'off'
            cont_attrs['dfuse-attr-time'] = cache_time
            cont_attrs['dfuse-dentry-time'] = cache_time
            cont_attrs['dfuse-ndentry-time'] = cache_time
            if intercept:
                build_time = 120
            self.dfuse.disable_wb_cache.value = True
        elif cache_mode == 'data':
            build_time = 60
            cont_attrs['dfuse-data-cache'] = True
            cont_attrs['dfuse-attr-time'] = '0'
            cont_attrs['dfuse-dentry-time'] = '0'
            cont_attrs['dfuse-ndentry-time'] = '0'
            if intercept:
                build_time = 120
            self.dfuse.disable_wb_cache.value = True
        elif cache_mode == 'nocache':
            build_time = 60
            cont_attrs['dfuse-data-cache'] = 'off'
            cont_attrs['dfuse-attr-time'] = '0'
            cont_attrs['dfuse-dentry-time'] = '0'
            cont_attrs['dfuse-ndentry-time'] = '0'
            self.dfuse.disable_wb_cache.value = True
            self.dfuse.disable_caching.value = True
        else:
            self.fail('Invalid cache_mode: {}'.format(cache_mode))

        for key, value in cont_attrs.items():
            daos_cmd.container_set_attr(pool=self.pool.uuid, cont=self.container.uuid,
                                        attr=key, val=value)

        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        mount_dir = self.dfuse.mount_dir.value
        build_dir = os.path.join(mount_dir, 'daos')

        remote_env = {}
        remote_env['PATH'] = '{}:$PATH'.format(os.path.join(mount_dir, 'venv', 'bin'))
        remote_env['VIRTUAL_ENV'] = os.path.join(mount_dir, 'venv')
        remote_env['COVFILE'] = os.environ['COVFILE']

        if intercept:
            remote_env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libioil.so')
            remote_env['D_LOG_FILE'] = '/var/tmp/daos_testing/daos-il.log'
            remote_env['DD_MASK'] = 'all'
            remote_env['DD_SUBSYS'] = 'all'
            remote_env['D_LOG_MASK'] = 'WARN,IL=WARN'

        envs = ['export {}={}'.format(env, value) for env, value in remote_env.items()]

        preload_cmd = ';'.join(envs)

        build_jobs = 6 * 5
        intercept_jobs = build_jobs
        if intercept:
            intercept_jobs = 1

        # Run the deps build in parallel for speed/coverage however the daos build itself does
        # not yet work, so run this part in serial.  The VMs have 6 cores each.
        cmds = ['python3 -m venv {}/venv'.format(mount_dir),
                'git clone https://github.com/daos-stack/daos.git {}'.format(build_dir),
                'git -C {} submodule init'.format(build_dir),
                'git -C {} submodule update'.format(build_dir),
                'python3 -m pip install pip --upgrade',
                'python3 -m pip install -r {}/requirements.txt'.format(build_dir),
                'scons -C {} --jobs {} --build-deps=only'.format(build_dir, build_jobs),
                'scons -C {} --jobs {}'.format(build_dir, intercept_jobs)]
        for cmd in cmds:
            command = '{};{}'.format(preload_cmd, cmd)
            # Use a short timeout for most commands, but vary the build timeout based on dfuse mode.
            timeout = 10 * 60
            if cmd.startswith('scons'):
                timeout = build_time * 60
            start = time.time()
            ret_code = general_utils.run_pcmd(self.hostlist_clients, command, verbose=True,
                                              timeout=timeout, expect_rc=0)
            elapsed = time.time() - start
            (minutes, seconds) = divmod(elapsed, 60)
            self.log.info('Completed in %d:%02d (%d%% of timeout)',
                          minutes, seconds, elapsed / timeout * 100)
            assert len(ret_code) == 1

            cmd_ret = ret_code[0]
            for (key, value) in cmd_ret.items():
                if key == 'stdout':
                    continue
                self.log.info('%s:%s', key, value)

            for line in cmd_ret['stdout']:
                self.log.info(line)

            if cmd_ret['exit_status'] == 0:
                continue

            fail_type = 'Failure to build'

            if cmd_ret['interrupted']:
                self.log.info('Command timed out')
                fail_type = 'Timeout building'

            self.log.error('BuildDaos Test Failed')
            if intercept:
                self.fail('{} over dfuse with il in mode {}.\n'.format(fail_type, cache_mode))
            else:
                self.fail('{} over dfuse in mode {}.\n'.format(fail_type, cache_mode))
