"""
  (C) Copyright 2022-2023 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from itertools import product

from ClusterShell.NodeSet import NodeSet

from dfuse_test_base import DfuseTestBase
from dfuse_utils import VerifyPermsCommand
from user_utils import get_chown_command
from run_utils import run_remote, command_as_user


class DfuseMUPerms(DfuseTestBase):
    """Verify dfuse multi-user basic permissions."""

    def test_dfuse_mu_perms(self):
        """Jira ID: DAOS-10854.
                    DAOS-10856.

        Test Description:
            Verify dfuse multi-user rwx permissions.
            Verify dfuse multi-user chown and chmod.
        Use cases:
            Create a pool.
            Create a container.
            Mount dfuse in multi-user mode.
            Verify all rwx permissions for dfuse owner : group user : other user.
            Create a sub-directory and give the group user ownership.
            Verify real rwx permissions for owner : dfuse user : other user.
            Create a sub-directory and give the other user ownership.
            Verify real rwx permissions for other user : None : dfuse user.

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

        # Save original users
        original_users = {
            'owner': verify_perms_cmd.owner.value,
            'group_user': verify_perms_cmd.group_user.value,
            'other_user': verify_perms_cmd.other_user.value
        }

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

        # Create a sub-directory owned by the group user and verify permissions
        verify_perms_cmd.update_params(
            owner=original_users['group_user'],
            group_user=original_users['owner'],
            other_user=original_users['other_user'])
        sub_dir = os.path.join(self.dfuse.mount_dir.value, 'dir1')
        self._create_dir_and_chown(
            client, sub_dir, create_as=dfuse_user, owner=verify_perms_cmd.owner.value)

        # Verify real permissions
        for _type in ('file', 'dir'):
            path = os.path.join(sub_dir, 'test_' + _type)
            self.log.info('Verifying real %s permissions on %s', _type, path)
            verify_perms_cmd.update_params(path=path, create_type=_type, verify_mode='real')
            verify_perms_cmd.run()
            self.log.info('Passed real %s permissions on %s', _type, path)

        # Create a sub-directory owned by the other user and verify permissions
        verify_perms_cmd.update_params(
            owner=original_users['other_user'],
            group_user=None,
            other_user=original_users['owner'])
        sub_dir = os.path.join(self.dfuse.mount_dir.value, 'dir2')
        self._create_dir_and_chown(
            client, sub_dir, create_as=dfuse_user,
            owner=verify_perms_cmd.owner.value, group='root')

        # Verify real permissions
        for _type in ('file', 'dir'):
            path = os.path.join(sub_dir, 'test_' + _type)
            self.log.info('Verifying real %s permissions on %s', _type, path)
            verify_perms_cmd.update_params(path=path, create_type=_type, verify_mode='real')
            verify_perms_cmd.run()
            self.log.info('Passed real %s permissions on %s', _type, path)

    def _create_dir_and_chown(self, client, path, create_as, owner, group=None):
        '''Create a directory and give some user and group ownership.

        Args:
            client (NodeSet): client to create directory on
            path (str): path to create
            create_as (str): user to run mkdir as
            owner (str): user to give ownership to
            group (str): group to give ownership to

        '''
        self.log.info('Creating directory: %s', path)
        command = command_as_user('mkdir ' + path, create_as)
        if not run_remote(self.log, client, command).passed:
            self.fail('Failed to create directory: {}'.format(path))

        if group:
            self.log.info('Giving ownership to %s:%s', owner, group)
        else:
            self.log.info('Giving ownership to %s', owner)
        command = command_as_user(get_chown_command(user=owner, group=group, file=path), 'root')
        if not run_remote(self.log, client, command).passed:
            self.fail('Failed to give ownership to {}'.format(owner))
        command = command_as_user('stat {}'.format(path), owner)
        if not run_remote(self.log, client, command).passed:
            self.fail('Failed to stat {}'.format(path))
