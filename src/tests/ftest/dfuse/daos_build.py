#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import distro
import general_utils
from collections import OrderedDict

from avocado import skip
from dfuse_test_base import DfuseTestBase
from exception_utils import CommandFailure

def skip_on_centos7():
    """Decorator to allow selective skipping of test"""
    dist = distro.linux_distribution()
    if dist[0] == 'CentOS Linux' and dist[1] == '7':
        return skip('Newer software distribution needed')

    def _do(func):
        def wrapper(*args, **kwargs):
            return func(*args, **kwargs)
        return wrapper
    return _do

class DaosBuild(DfuseTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Build DAOS over dfuse

    :avocado: recursive
    """

    @skip_on_centos7()
    def test_daos_build(self):
        """Jira ID: DAOS-8937.

        Test Description:
            This test builds DAOS on a dfuse filesystem.
        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=daosio,dfuse
        :avocado: tags=dfusedaosbuild
        """

        # Create a pool, container and start dfuse.
        self.add_pool(connect=False)
        self.add_container(self.pool)

        daos_cmd = self.get_daos_command()

        cont_attrs = OrderedDict()

        cache_mode = self.params.get('name', '/run/dfuse/*')

        # How long to cache things for, if caching is enabled.
        cache_time = '30m'
        build_time = 15

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
            build_time = 150
            cont_attrs['dfuse-data-cache'] = 'off'
            cont_attrs['dfuse-attr-time'] = '0'
            cont_attrs['dfuse-dentry-time'] = '0'
            cont_attrs['dfuse-ndentry-time'] = '0'

        for key, value in cont_attrs.items():
            daos_cmd.container_set_attr(pool=self.pool.uuid, cont=self.container.uuid,
                                        attr=key, val=value)

        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        mount_dir = self.dfuse.mount_dir.value
        build_dir = os.path.join(mount_dir, 'daos')

        intercept = self.params.get('use_intercept', '/run/intercept/*', default=False)

        remote_env = OrderedDict()
        remote_env['PATH'] = f'{mount_dir}/venv/bin:$PATH'
        remote_env['VIRTUAL_ENV'] = f'{mount_dir}/venv'

        if intercept:
            remote_env['LD_PRELOAD'] = '/usr/lib64/libioil.so'
            remote_env['D_LOG_FILE'] = '/var/tmp/daos_testing/daos-il.log'
            remote_env['DD_MASK'] = 'all'
            remote_env['DD_SUBSYS'] = 'all'
            remote_env['D_LOG_MASK'] = 'INFO,IL=DEBUG'

        envs = []
        for env, value in remote_env.items():
            envs.append('export {}={}'.format(env, value))

        preload_cmd = ';'.join(envs)

        cmds = [f'python3 -m venv {mount_dir}/venv',
                f'git clone https://github.com/daos-stack/daos.git {build_dir}',
                f'git -C {build_dir} submodule init',
                f'git -C {build_dir} submodule update',
                f'git -C {build_dir} checkout daos-build-test-mux',
                'python3 -m pip install pip --upgrade',
                f'python3 -m pip install -r {build_dir}/requirements.txt',
                f'scons -C {build_dir} --jobs 50 build --build-deps=yes']
        for cmd in cmds:
            try:
                command = '{};{}'.format(preload_cmd, cmd)
                # Use a 10 minute timeout for most commands, but vary the build timeout based on
                # the dfuse mode.
                timeout = 10 * 60
                if cmd.startswith('scons'):
                    timeout = build_time * 60
                ret_code = general_utils.pcmd(self.hostlist_clients, command, timeout=timeout)
                if 0 in ret_code:
                    continue
                self.log.info(ret_code)
                raise CommandFailure("Error running '{}'".format(cmd))
            except CommandFailure as error:
                self.log.error("BuildDaos Test Failed: %s", str(error))
                self.fail("Test was expected to pass but it failed.\n")
