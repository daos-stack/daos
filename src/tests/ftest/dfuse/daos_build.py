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

        scons = 'scons-3'
        dist = distro.linux_distribution()
        if dist[0] == 'openSUSE Leap':
            scons = 'scons'

        # Create a pool, container and start dfuse.
        self.add_pool(connect=False)
        self.add_container(self.pool)

        daos_cmd = self.get_daos_command()

        cont_attrs = OrderedDict()
        cont_attrs['dfuse-data-cache'] = 'off'
        cont_attrs['dfuse-attr-time'] = '60s'
        cont_attrs['dfuse-dentry-time'] = '60s'
        cont_attrs['dfuse-ndentry-time'] = '60s'

        for key, value in cont_attrs.items():
            daos_cmd.container_set_attr(pool=self.pool.uuid, cont=self.container.uuid,
                                        attr=key, val=value)

        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        mount_dir = self.dfuse.mount_dir.value
        build_dir = os.path.join(mount_dir, 'daos')

        # This will apply to SCons, but not sub-commands as scons strips the environment.

        remote_env = OrderedDict()
        remote_env['LD_PRELOAD'] = '/usr/lib64/libioil.so'
        remote_env['D_LOG_FILE'] = '/var/tmp/daos_testing/daos-il.log'
        remote_env['DD_MASK'] = 'all'
        remote_env['DD_SUBSYS'] = 'all'
#        remote_env['D_IL_REPORT'] = '2'
        remote_env['D_LOG_MASK'] = 'INFO,IL=DEBUG'

        envs = []
        for env, value in remote_env.items():
            envs.append('export {}={}'.format(env, value))

        preload_cmd = ';'.join(envs)

        cmds = ['git clone https://github.com/daos-stack/daos.git {}'.format(build_dir),
                'git -C {} submodule init'.format(build_dir),
                'git -C {} submodule update'.format(build_dir),
                '{} -C {} --jobs 50 build --build-deps=yes'.format(scons, build_dir)]
        for cmd in cmds:
            try:
                command = '{};{}'.format(preload_cmd, cmd)
                ret_code = general_utils.pcmd(self.hostlist_clients, command, timeout=60 * 60 * 4)
                if 0 in ret_code:
                    continue
                self.log.info(ret_code)
                raise CommandFailure("Error running '{}'".format(cmd))
            except CommandFailure as error:
                self.log.error("BuildDaos Test Failed: %s", str(error))
                self.fail("Test was expected to pass but it failed.\n")
