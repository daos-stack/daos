"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from getpass import getuser

from ClusterShell.NodeSet import NodeSet

from run_utils import run_remote, command_as_user
from dfuse_test_base import DfuseTestBase


class DfuseMUMount(DfuseTestBase):
    """Verifies multi-user dfuse mounting"""

    def test_dfuse_mu_mount_basic(self):
        """JIRA ID: DAOS-6540, DAOS-8135.

        Use Cases:
            Verify non-dfuse user cannot mount the container without ACLs in single-user mode.
            Verify non-dfuse user cannot mount the container without ACLs in multi-user mode.
            Verify stat as dfuse user in single-user mode succeeds.
            Verify stat as non-dfuse user in single-user mode fails.
            Verify stat as dfuse user in multi-user mode succeeds.
            Verify stat as non-dfuse user in multi-user mode fails.
            Verify non-dfuse user can mount the container with ACLs in single-user mode.
            Verify non-dfuse user can mount the container with ACLs in multi-user mode.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_mu,daos_cmd
        :avocado: tags=DfuseMUMount,test_dfuse_mu_mount_basic
        """
        self.log.info('Creating a pool and container for dfuse')
        pool = self.get_pool(connect=False)
        cont = self.get_container(pool, label='root_cont')

        # Setup dfuse
        self.load_dfuse(self.hostlist_clients)
        self.dfuse.update_params(pool=pool.identifier, cont=cont.label.value)
        root_dir = self.dfuse.mount_dir.value

        # Use a different log file for each user
        root_log_file = self.dfuse.env["D_LOG_FILE"] + ".root"
        dfuse_user_log_file = self.dfuse.env["D_LOG_FILE"] + "." + getuser()

        # For verifying expected permission failure
        def _check_fail(result):
            for stdout in result.all_stdout.values():
                if 'DER_NO_PERM' not in stdout:
                    self.fail('Expected mount as root to fail without ACLs in single-user mode')

        self.log.info('Verify root cannot mount the container without ACLs in single-user mode')
        self.dfuse.update_params(multi_user=False)
        self.dfuse.run_user = 'root'
        self.dfuse.env["D_LOG_FILE"] = root_log_file
        self.dfuse.run(check=False, mount_callback=_check_fail)
        self.dfuse.stop()

        self.log.info('Verify root cannot mount the container without ACLs in multi-user mode')
        self.dfuse.update_params(multi_user=True)
        self.dfuse.run_user = 'root'
        self.dfuse.env["D_LOG_FILE"] = root_log_file
        self.dfuse.run(check=False, mount_callback=_check_fail)
        self.dfuse.stop()

        self.log.info('Mounting dfuse in single-user mode')
        self.dfuse.update_params(multi_user=False)
        self.dfuse.run_user = None  # Current user
        self.dfuse.env["D_LOG_FILE"] = dfuse_user_log_file
        self.dfuse.run()

        self.log.info('Verify stat as dfuse user in single-user mode succeeds')
        command = 'stat {}'.format(root_dir)
        if not run_remote(self.log, self.hostlist_clients, command).passed:
            self.fail('Failed to stat in single-user mode')

        self.log.info('Verify stat as root user in single-user mode fails')
        command = command_as_user('stat {}'.format(root_dir), 'root')
        if run_remote(self.log, self.hostlist_clients, command).passed:
            self.fail('Expected stat to fail as root in single-user mode')

        self.log.info('Re-mounting dfuse in multi-user mode')
        self.dfuse.stop()
        self.dfuse.update_params(multi_user=True)
        self.dfuse.run()

        self.log.info('Verify stat as dfuse user in multi-user mode succeeds')
        command = 'stat {}'.format(root_dir)
        if not run_remote(self.log, self.hostlist_clients, command).passed:
            self.fail('Failed to stat in multi-user mode')

        self.log.info('Verify stat as root user in multi-user mode succeeds')
        command = command_as_user('stat {}'.format(root_dir), 'root')
        if not run_remote(self.log, self.hostlist_clients, command).passed:
            self.fail('Failed to stat as root in multi-user mode')

        # Cleanup leftover dfuse
        self.dfuse.stop()

        # Give root permission to read the pool
        pool.update_acl(False, entry="A::root@:r")

        # Give root permission to read the container and access properties
        cont.update_acl(entry="A::root@:rt")

        self.log.info('Verify root can mount the container with ACLs in single-user mode')
        self.dfuse.update_params(multi_user=False)
        self.dfuse.run_user = 'root'
        self.dfuse.env["D_LOG_FILE"] = root_log_file
        self.dfuse.run()
        self.dfuse.stop()

        self.log.info('Verify root can mount the container with ACLs in muli-user mode')
        self.dfuse.update_params(multi_user=True)
        self.dfuse.run_user = 'root'
        self.dfuse.env["D_LOG_FILE"] = dfuse_user_log_file
        self.dfuse.run()
        self.dfuse.stop()

    def test_dfuse_mu_mount_uns(self):
        """JIRA ID: DAOS-10859.

        Use Cases:
            Verify a container created by non-dfuse user, with a UNS path in dfuse, automatically
                grants container ACLs to the dfuse user.
            Verify the non-dfuse user and dfuse user can access the container through dfuse.
            Verify that revoking the container ACLs for only the dfuse user prevents access
                through dfuse for both the dfuse user and non-dfuse user.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_mu,daos_cmd
        :avocado: tags=DfuseMUMount,test_dfuse_mu_mount_uns
        """
        dfuse_user = getuser()

        self.log.info('Creating a pool and container for dfuse')
        pool1 = self.get_pool(connect=False)
        cont1 = self.get_container(pool1, label='root_cont')

        self.log.info('Starting dfuse with the current user in multi-user mode')
        self.load_dfuse(self.hostlist_clients)
        self.dfuse.update_params(pool=pool1.identifier, cont=cont1.label.value, multi_user=True)
        root_dir = self.dfuse.mount_dir.value
        self.dfuse.run()

        self.log.info('Creating a second pool')
        pool2 = self.get_pool(connect=False)

        self.log.info('Giving root permission to create containers in both pools')
        pool1.update_acl(False, entry="A::root@:rw")
        pool2.update_acl(False, entry="A::root@:rw")

        # DaosCommand cannot be used directly because this needs to run remotely
        daos_command = self.get_daos_command()
        daos_path = os.path.join(daos_command.command_path, daos_command.command)
        first_client = NodeSet(self.hostlist_clients[0])

        def _verify_uns(pool_label, cont_label):
            """Helper function for varying pool/container."""
            cont_path = os.path.join(root_dir, cont_label)
            command = command_as_user(
                '{} container create {} --type POSIX --path {}'.format(
                    daos_path, pool_label, cont_path),
                'root')
            if not run_remote(self.log, first_client, command).passed:
                self.fail('Failed to create sub-container as root in multi-user mode')

            self.log.info('Verify dfuse user was automatically given ACLs for the new container')
            expected_acl = 'A::{}@:rwt'.format(dfuse_user)
            self.log.info('Expected ACL: %s', expected_acl)
            command = command_as_user(
                '{} container get-acl --path {}'.format(daos_path, cont_path), 'root')
            result = run_remote(self.log, first_client, command)
            if not result.passed:
                self.fail('Failed to get ACLs for container created by root')
            for stdout in result.all_stdout.values():
                if expected_acl not in stdout:
                    self.fail('Expected ACL for container created by root: {}'.format(expected_acl))

            self.log.info('Verify dfuse user can access the container created by root')
            command = '{} container get-prop --path {}'.format(daos_path, cont_path)
            if not run_remote(self.log, first_client, command).passed:
                self.fail('Failed to get sub-container properties in multi-user mode')

            self.log.info('Verify dfuse user can read the container created by root')
            command = 'ls -l {}'.format(cont_path)
            if not run_remote(self.log, first_client, command).passed:
                self.fail('Failed to read container created by root, as dfuse user')

            self.log.info('Verify root can read its own container')
            command = command_as_user('ls -l {}'.format(cont_path), 'root')
            if not run_remote(self.log, first_client, command).passed:
                self.fail('Failed to read container created by root, as root')

            self.log.info('Revoking ACLs for just dfuse user on the container created by root')
            principal = 'u:{}@'.format(dfuse_user)
            command = command_as_user(
                '{} container delete-acl --path {} --principal {}'.format(
                    daos_path, cont_path, principal),
                'root')
            if not run_remote(self.log, first_client, command).passed:
                self.fail('Failed to revoke ACLs on container created by root')

            self.log.info('Restarting dfuse to pick up ACL changes')
            self.dfuse.stop()
            self.dfuse.run()

            self.log.info('Verifying dfuse user can no longer read the container through dfuse')
            command = 'ls -l {}'.format(cont_path)
            if run_remote(self.log, first_client, command).passed:
                self.fail('Expected ls to fail on container created by root, as dfuse user')

            self.log.info('Verifying root can no longer read the container through dfuse')
            command = command_as_user('ls -l {}'.format(cont_path), 'root')
            if run_remote(self.log, first_client, command).passed:
                self.fail('Expected ls to fail on container created by root, as root')

        self.log.info('Verify UNS sub-container create as root - in dfuse pool')
        _verify_uns(pool_label="", cont_label='pool1_root_cont')

        self.log.info('Verify UNS sub-container create as root - in different pool')
        _verify_uns(pool_label=pool2.identifier, cont_label='pool2_root_cont')
