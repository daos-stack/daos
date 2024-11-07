"""
  (C) Copyright 2022-2024 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re
import time
from itertools import product

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from dfuse_utils import VerifyPermsCommand, get_dfuse, start_dfuse
from run_utils import command_as_user, run_remote
from user_utils import get_chown_command


class DfuseMUPerms(TestWithServers):
    """Verify dfuse multi-user basic permissions.

    :avocado: recursive
    """

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
        cont = self.get_container(pool, daos=daos_command)

        # Run dfuse as dfuse_user
        dfuse = get_dfuse(self, client)
        dfuse.run_user = dfuse_user
        start_dfuse(self, dfuse, pool, cont)

        # Verify each permission mode and entry type
        for _mode, _type in product(('simple', 'real'), ('file', 'dir')):
            path = os.path.join(dfuse.mount_dir.value, 'test_' + _type)
            self.log_step(f'Verifying {_mode} {_type} permissions on {path}')
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
            self.log_step(f'Verifying real {_type} permissions on {path}')
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
            self.log_step(f'Verifying real {_type} permissions on {path}')
            verify_perms_cmd.update_params(path=path, create_type=_type, verify_mode='real')
            verify_perms_cmd.run()
            self.log.info('Passed real %s permissions on %s', _type, path)

        self.log.info('Test passed')

    def _create_dir_and_chown(self, client, path, create_as, owner, group=None):
        """Create a directory and give some user and group ownership.

        Args:
            client (NodeSet): client to create directory on
            path (str): path to create
            create_as (str): user to run mkdir as
            owner (str): user to give ownership to
            group (str): group to give ownership to

        """
        self.log_step('Creating directory: %s', path)
        command = command_as_user('mkdir ' + path, create_as)
        if not run_remote(self.log, client, command).passed:
            self.fail(f'Failed to create directory: {path}')

        if group:
            self.log_step(f'Giving ownership to {owner}:{group}')
        else:
            self.log_step(f'Giving ownership to {owner}')
        command = command_as_user(get_chown_command(user=owner, group=group, file=path), 'root')
        if not run_remote(self.log, client, command).passed:
            self.fail(f'Failed to give ownership to {owner}')
        command = command_as_user(f'stat {path}', owner)
        if not run_remote(self.log, client, command).passed:
            self.fail(f'Failed to stat {path}')

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
        cont = self.get_container(pool, daos=daos_command)

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

        self.log.info('Test passed')

    def run_test_il(self, il_lib=None):
        """Jira ID: DAOS-10857.

        Test Description:
            Verify dfuse multi-user rwx permissions with the interception library.
        Use cases:
            Create a pool.
            Create a container.
            Mount dfuse in multi-user mode.
            Create a file and directory in dfuse.
            Revoke "other" file and directory permissions.
            Verify other users do not have access with or without IL.
            Grant "other" file and directory permissions.
            Verify other users have access with or without IL.
            Grant other users pool 'r' and container 'rwt' ACLs.
            Verify other users have access with or without IL.
            Revoke "other" file and directory permissions.
            Verify other users do not have access with or without IL.
            For each case, verify IL debug messages for files.

        """
        if il_lib is None:
            self.fail('il_lib is not defined.')
        # Setup the verify command. Only test with the owner and group_user
        verify_perms_cmd = VerifyPermsCommand(self.hostlist_clients)
        verify_perms_cmd.get_params(self)
        verify_perms_cmd.update_params(group_user=None, verify_mode='real', no_chmod=True)

        # Use the owner to mount dfuse
        dfuse_user = verify_perms_cmd.owner.value

        # Create a pool and give dfuse_user access
        pool = self.get_pool(connect=False)
        pool.update_acl(False, 'A::{}@:rw'.format(dfuse_user))

        # Create a container as dfuse_user
        daos_command = self.get_daos_command()
        daos_command.run_user = dfuse_user
        cont = self.get_container(pool, daos=daos_command)

        # Run dfuse as dfuse_user
        dfuse = get_dfuse(self, self.hostlist_clients)
        dfuse.run_user = dfuse_user
        start_dfuse(self, dfuse, pool=pool, container=cont)

        # The user we'll be changing permissions for
        other_user = verify_perms_cmd.other_user.value

        env_without_il = verify_perms_cmd.env.copy()
        env_with_il = env_without_il.copy()
        env_with_il.update({
            'LD_PRELOAD': os.path.join(self.prefix, 'lib64', il_lib),
            'D_IL_REPORT': -1,  # Log all intercepted calls
            'D_IL_NO_BYPASS': '1'
        })

        def _verify(use_il, expected_il_messages, expect_der_no_perm):
            """Verify permissions for a given configuration.

            Args:
                use_il (bool): whether to use the interception library
                expected_il_messages (int): the number of expected interception messages.
                    Overwritten to 0 for directories
                expect_der_no_perm (bool): whether DER_NO_PERM is expected
                    through the interception library. Overwritten to False for directories

            """
            verify_perms_cmd.env = env_with_il if use_il else env_without_il
            result = verify_perms_cmd.run()

            # the output of libioil.so and libpil4dfs.so are different.
            if il_lib == 'libpil4dfs.so':
                return

            num_il_messages = 0
            found_der_no_perm = False
            for stdout in result.all_stdout.values():
                num_il_messages += len(re.findall(r'\[libioil\] Intercepting', stdout))
                found_der_no_perm = found_der_no_perm or ('DER_NO_PERM' in stdout)

            # Only IO is intercepted, so IL with directories should be silent
            if entry_type == 'dir':
                expected_il_messages = 0
                expect_der_no_perm = False

            self.assertEqual(
                expected_il_messages, num_il_messages,
                'Expected {} IL messages but got {}'.format(expected_il_messages, num_il_messages))

            if expect_der_no_perm and not found_der_no_perm:
                self.fail('Expected DER_NO_PERM with IL in stdout')
            elif found_der_no_perm and not expect_der_no_perm:
                self.fail('Unexpected DER_NO_PERM with IL found in stdout')

        # Verify file and dir permissions
        for entry_type in ('file', 'dir'):
            dfuse_entry_path = os.path.join(dfuse.mount_dir.value, entry_type)
            create_cmd = 'touch' if entry_type == 'file' else 'mkdir'

            self.log_step('Creating a test %s in dfuse', entry_type)
            command = command_as_user('{} "{}"'.format(create_cmd, dfuse_entry_path), dfuse_user)
            if not run_remote(self.log, self.hostlist_clients, command).passed:
                self.fail('Failed to create test {}'.format(entry_type))

            verify_perms_cmd.update_params(path=dfuse_entry_path)

            # Revoke POSIX permissions
            posix_perms = {'file': '600', 'dir': '600'}[entry_type]
            self.log_step(f'Setting {entry_type} POSIX permissions to {posix_perms}')
            command = command_as_user(
                'chmod {} "{}"'.format(posix_perms, dfuse_entry_path), dfuse_user)
            if not run_remote(self.log, self.hostlist_clients, command).passed:
                self.fail('Failed to chmod test {}'.format(entry_type))

            # Without pool/container ACLs, access is based on POSIX perms,
            # which the user also doesn't have
            verify_perms_cmd.update_params(perms=posix_perms)
            self.log_step('Verify - no perms - not using IL')
            _verify(use_il=False, expected_il_messages=0, expect_der_no_perm=False)
            self.log_step('Verify - no perms - using IL')
            _verify(use_il=True, expected_il_messages=2, expect_der_no_perm=False)

            # Give the user POSIX perms
            posix_perms = {'file': '606', 'dir': '505'}[entry_type]
            self.log_step(f'Setting {entry_type} POSIX permissions to {posix_perms}')
            command = command_as_user(
                'chmod {} "{}"'.format(posix_perms, dfuse_entry_path), dfuse_user)
            if not run_remote(self.log, self.hostlist_clients, command).passed:
                self.fail('Failed to chmod test {}'.format(entry_type))

            # With POSIX perms only, access is based on POSIX perms whether using IL or not
            verify_perms_cmd.update_params(perms=posix_perms)
            self.log_step('Verify - POSIX perms only - not using IL')
            _verify(use_il=False, expected_il_messages=0, expect_der_no_perm=False)
            self.log_step('Verify - POSIX perms only - using IL')
            _verify(use_il=True, expected_il_messages=2, expect_der_no_perm=True)

            # Give the user pool/container ACL perms
            self.log_step('Giving %s pool "r" ACL permissions', other_user)
            pool.update_acl(use_acl=False, entry="A::{}@:r".format(other_user))
            self.log_step('Giving %s container "rwt" ACL permissions', other_user)
            cont.update_acl(entry="A::{}@:rwt".format(other_user))

            # With POSIX perms and ACLs, open is based on POSIX, but IO is based on ACLs
            self.log_step('Verify - POSIX and ACL perms - not using IL')
            _verify(use_il=False, expected_il_messages=0, expect_der_no_perm=False)
            self.log_step('Verify - POSIX and ACL perms - using IL')
            _verify(use_il=True, expected_il_messages=4, expect_der_no_perm=False)

            # Revoke POSIX permissions
            posix_perms = {'file': '600', 'dir': '00'}[entry_type]
            self.log_step(f'Setting {entry_type} POSIX permissions to {posix_perms}')
            command = command_as_user(
                'chmod {} "{}"'.format(posix_perms, dfuse_entry_path), dfuse_user)
            if not run_remote(self.log, self.hostlist_clients, command).passed:
                self.fail('Failed to chmod test {}'.format(entry_type))

            # Without POSIX permissions, pool/container ACLs don't matter since open requires POSIX
            verify_perms_cmd.update_params(perms=posix_perms)
            self.log_step('Verify - ACLs only - not using IL')
            _verify(use_il=False, expected_il_messages=0, expect_der_no_perm=False)
            self.log_step('Verify - ACLs only - using IL')
            _verify(use_il=True, expected_il_messages=2, expect_der_no_perm=False)

    def test_dfuse_mu_perms_ioil(self):
        """
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_mu,ioil,verify_perms
        :avocado: tags=DfuseMUPerms,test_dfuse_mu_perms_ioil
        """
        self.run_test_il(il_lib='libioil.so')
        self.log.info('Test passed')

    def test_dfuse_mu_perms_pil4dfs(self):
        """
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_mu,pil4dfs,verify_perms
        :avocado: tags=DfuseMUPerms,test_dfuse_mu_perms_pil4dfs
        """
        self.run_test_il(il_lib='libpil4dfs.so')
        self.log.info('Test passed')
