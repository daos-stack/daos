#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import general_utils

from dfuse_test_base import DfuseTestBase
from command_utils import CommandFailure

class BuildDaos(DfuseTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Build DAOS over dfuse

    :avocado: recursive
    """

    def test_daos_build(self):
        """Jira ID: DAOS-8937.

        Test Description:
            This tests builds DAOS on a dfuse filesystem.
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
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        mount_dir = self.dfuse.mount_dir.value
        build_dir = os.path.join(mount_dir, 'daos')

        cmds = ['git clone https://github.com/daos-stack/daos.git {}'.format(build_dir),
                'git -C {} submodule init'.format(build_dir),
                'git -C {} submodule update'.format(build_dir),
                'sudo yum -y install meson',
                'scons-3 -C {} build --build-deps=yes'.format(build_dir)]
        for cmd in cmds:
            try:
                ret_code = general_utils.pcmd(self.hostlist_clients, cmd, timeout=3600)
                print(ret_code)
                if 0 in ret_code:
                    continue
                raise CommandFailure("Error running '{}'".format(cmd))
                # report error if any command fails
            except CommandFailure as error:
                self.log.error("BashCmd Test Failed: %s", str(error))
                self.fail("Test was expected to pass but it failed.\n")
