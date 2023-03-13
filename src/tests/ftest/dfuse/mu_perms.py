"""
  (C) Copyright 2022-2023 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from itertools import product
import time

from ClusterShell.NodeSet import NodeSet

from dfuse_test_base import DfuseTestBase
from dfuse_utils import get_dfuse, start_dfuse, VerifyPermsCommand
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
        :avocado: tags=dfuse,dfuse_mu,verify_perms,daos_cmd
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
        dfuse = get_dfuse(self, client)
        dfuse.run_user = dfuse_user
        start_dfuse(self, dfuse, pool=pool, container=cont)

        # Verify each permission mode and entry type
        for _mode, _type in product(('simple', 'real'), ('file', 'dir')):
            path = os.path.join(dfuse.mount_dir.value, 'test_' + _type)
            self.log.info('Verifying %s %s permissions on %s', _mode, _type, path)
            verify_perms_cmd.update_params(path=path, create_type=_type, verify_mode=_mode)
            verify_perms_cmd.run()
            self.log.info('Passed %s %s permissions on %s', _mode, _type, path)

        # Create a sub-directory owned by the group user and verify permissions
        verify_perms_cmd.update_params(
            owner=original_users['group_user'],
            group_user=original_users['owner'],
            other_user=original_users['other_user'])
        sub_dir = os.path.join(dfuse.mount_dir.value, 'dir1')
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
        sub_dir = os.path.join(dfuse.mount_dir.value, 'dir2')
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

        # Stop dfuse instances. Needed until containers are cleanup with with register_cleanup
        dfuse.stop()

    def _create_dir_and_chown(self, client, path, create_as, owner, group=None):
        """Create a directory and give some user and group ownership.

        Args:
            client (NodeSet): client to create directory on
            path (str): path to create
            create_as (str): user to run mkdir as
            owner (str): user to give ownership to
            group (str): group to give ownership to

        """
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

    def test_dfuse_mu_perms_cache(self):
        """Jira ID: DAOS-10858.

        Test Description:
            Verify dfuse multi-user rwx permissions with caching enabled.
        Use cases:
            Create a pool.
            Create a container.
            Mount dfuse1 in multi-user mode with caching enabled.
            Mount dfuse2 in multi-user mode with caching enabled.
            Create a file in dfuse1.
            Grant permissions on the file.
            Verify permission access in dfuse2.
            Revoke permissions in dfuse2.
            Wait for cache expiration.
            Verify permission denied in dfuse2.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_mu,verify_perms
        :avocado: tags=DfuseMUPerms,test_dfuse_mu_perms_cache
        """
        cache_time = self.params.get('cache_time', '/run/test_dfuse_mu_perms_cache/*')

        def _wait_for_cache_expiration():
            """Wait cache_time + 1 seconds to account for rounding errors, etc."""
            self.log.info('Waiting %s + 1 seconds for cache expiration', cache_time)
            time.sleep(int(cache_time) + 1)

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

        self.log.info('Setting dfuse cache time to %s', cache_time)
        cont.set_attr(attrs={
            'dfuse-data-cache': 'off',
            'dfuse-attr-time': cache_time,
            'dfuse-dentry-time': cache_time,
            'dfuse-ndentry-time': cache_time
        })

        self.log.info('Starting first dfuse instance')
        dfuse1 = get_dfuse(self, client, namespace='/run/dfuse_with_caching/*')
        dfuse1.update_params(mount_dir=dfuse1.mount_dir.value + '_dfuse1')
        dfuse1.run_user = dfuse_user
        start_dfuse(self, dfuse1, pool=pool, container=cont)

        self.log.info('Starting second dfuse instance')
        dfuse2 = get_dfuse(self, client, namespace='/run/dfuse_with_caching/*')
        dfuse2.update_params(mount_dir=dfuse2.mount_dir.value + '_dfuse2')
        dfuse2.run_user = dfuse_user
        start_dfuse(self, dfuse2, pool=pool, container=cont)

        # Verify file and dir permissions
        for entry_type in ('file', 'dir'):
            dfuse1_entry_path = os.path.join(dfuse1.mount_dir.value, entry_type)
            dfuse2_entry_path = os.path.join(dfuse2.mount_dir.value, entry_type)
            perms = '444' if entry_type == 'file' else '555'
            create_cmd = 'touch' if entry_type == 'file' else 'mkdir'

            self.log.info('Creating a test %s in the first dfuse instance', entry_type)
            command = command_as_user('{} "{}"'.format(create_cmd, dfuse1_entry_path), dfuse_user)
            if not run_remote(self.log, client, command).passed:
                self.fail('Failed to create test {}'.format(entry_type))

            self.log.info('Setting %s permissions to %s', entry_type, perms)
            command = command_as_user('chmod {} "{}"'.format(perms, dfuse1_entry_path), dfuse_user)
            if not run_remote(self.log, client, command).passed:
                self.fail('Failed to chmod test {}'.format(entry_type))
            _wait_for_cache_expiration()

            self.log.info('Verifying %s permissions with %s', entry_type, perms)
            verify_perms_cmd.update_params(
                path=dfuse2_entry_path, verify_mode='real', perms=perms, no_chmod=True)
            verify_perms_cmd.run()

            self.log.info('Revoking %s permissions', entry_type)
            command = command_as_user('chmod 000 "{}"'.format(dfuse1_entry_path), dfuse_user)
            if not run_remote(self.log, client, command).passed:
                self.fail('Failed to chmod test {}'.format(entry_type))
            _wait_for_cache_expiration()

            self.log.info('Verifying %s permissions after cache expiration', entry_type)
            verify_perms_cmd.update_params(
                path=dfuse2_entry_path, verify_mode='real', perms='000', no_chmod=True)
            verify_perms_cmd.run()

        # Stop dfuse instances. Needed until containers are cleaned up with with register_cleanup
        dfuse1.stop()
        dfuse2.stop()
