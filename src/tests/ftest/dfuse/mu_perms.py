"""
  (C) Copyright 2022 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from itertools import product

from ClusterShell.NodeSet import NodeSet

from dfuse_test_base import DfuseTestBase
from dfuse_utils import VerifyPermsCommand


class DfuseMUPerms(DfuseTestBase):
    """Verify dfuse multi-user basic permissions."""

    def test_dfuse_mu_perms(self):
        """Jira ID: DAOS-10854.

        Test Description:
            Verify dfuse multi-user permissions.
        Use cases:
            Create a pool.
            Create a container.
            Mount dfuse in multi-user mode.
            Verify all rwx permissions for the owner and other users.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_mu,verify_perms
        :avocado: tags=DfuseMUPerms,test_dfuse_mu_perms
        """
        # Use a single client for file/dir operations
        client = NodeSet(self.hostlist_clients[0])

        # Setup the verify command
        verify_perms_cmd = VerifyPermsCommand(client)
        verify_perms_cmd.get_params(self)

        # Use the owner to mount dfuse
        dfuse_user = verify_perms_cmd.owner.value

        # Create a pool and give dfuse_user access
        pool = self.get_pool(connect=False)
        pool.update_acl(False, 'A::{}@:rw'.format(dfuse_user))

        # Create a container as dfuse_user
        daos_command = self.get_daos_command()
        daos_command.run_user = dfuse_user
        cont = self.get_container(pool, daos_command=daos_command)

        # Run dfuse as dfuse_user
        self.load_dfuse(client)
        self.dfuse.run_user = dfuse_user
        self.start_dfuse(client, pool, cont)

        # Verify each permission mode and entry type
        for _mode, _type in product(('simple', 'real'), ('file', 'dir')):
            path = os.path.join(self.dfuse.mount_dir.value, 'test_' + _type)
            self.log.info('Verifying %s %s permissions on %s', _mode, _type, path)
            verify_perms_cmd.update_params(path=path, create_type=_type, verify_mode=_mode)
            verify_perms_cmd.run()
            self.log.info('Passed %s %s permissions on %s', _mode, _type, path)
