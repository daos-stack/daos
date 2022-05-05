#!/usr/bin/python
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import stat
import tempfile
import general_utils
from collections import OrderedDict

from dfuse_test_base import DfuseTestBase
from exception_utils import CommandFailure


class ILShell(DfuseTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Run a shell script using the  Interception Library

    :avocado: recursive
    """

    def test_daos_build(self):
        """Jira ID: DAOS-8937.

        Test Description:
            This test runs a shell script.
        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=dfuseilshell
        """

        # Create a pool, container and start dfuse.
        self.add_pool(connect=False)
        self.add_container(self.pool)

        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        mount_dir = self.dfuse.mount_dir.value

        shell_script = b"""/bin/bash

set -e
FILES=$(/bin/ls -l)
echo $FILES

FILES=$(/bin/ls -l /tmp)
echo $FILES

# Write to a file using re-direct.
dd if=/dev/urandom bs=1k count=2 > out-file

echo $1
"""

        remote_env = OrderedDict()
        remote_env['LD_PRELOAD'] = '/usr/lib64/libioil.so'
        remote_env['D_LOG_FILE'] = '/var/tmp/daos_testing/daos-il.log'
        remote_env['DD_MASK'] = 'all'
        remote_env['DD_SUBSYS'] = 'all'
        remote_env['D_IL_REPORT'] = '2'
        remote_env['D_LOG_MASK'] = 'INFO,IL=DEBUG'

        envs = []
        for env, value in remote_env.items():
            envs.append('export {}={}'.format(env, value))
        preload_cmd = ';'.join(envs)

        with tempfile.NamedTemporaryFile() as script:
            script.write(shell_script)
            os.fchmod(script, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)

            try:
                command = '{};{} {}'.format(preload_cmd, script.name, mount_dir)
                ret_code = general_utils.pcmd(self.hostlist_clients, command, timeout=1500)
                if 0 in ret_code:
                    return
                self.log.info(ret_code)
                raise CommandFailure('Error running script')
            except CommandFailure as error:
                self.log.error("BuildDaos Test Failed: %s", str(error))
                self.fail("Test was expected to pass but it failed.\n")
