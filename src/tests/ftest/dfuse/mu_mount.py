"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from ClusterShell.NodeSet import NodeSet

from run_utils import run_remote, command_as_user
from dfuse_test_base import DfuseTestBase


class DfuseMUMount(DfuseTestBase):
    """Verifies multi-user dfuse mounting"""

    def test_dfuse_mu_mount(self):
        """This test simply starts a filesystem and checks file ownership.

        Use Cases:
            Verify non-dfuse user cannot mount the container without ACLs in single-user mode.
            Verify non-dfuse user cannot mount the container without ACLs in multi-user mode.
            Verify stat as dfuse user in single-user mode succeeds.
            Verify stat as non-dfuse user in single-user mode fails.
            Verify stat as dfuse user in multi-user mode succeeds.
            Verify stat as non-dfuse user in multi-user mode fails.
            Verify non-dfuse user can mount the container with ACLs in single-user mode.
            Verify non-dfuse user can mount the container with ACLs in multi-user mode.
            Verify UNS sub-container creation as non-root user in multi-user mode succeeds.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_mu
        :avocado: tags=daos_cmd
        :avocado: tags=DfuseMUMount,test_dfuse_mu_mount
        """
        # Create a pool and container for dfuse
        pool = self.get_pool(connect=False)
        cont = self.get_container(pool, label='root_cont')

        # Setup dfuse
        self.load_dfuse(self.hostlist_clients)
        self.dfuse.update_params(pool=pool.identifier, cont=cont.label.value)
        root_dir = self.dfuse.mount_dir.value

        # For verifying expected permission failure
        def _check_fail(result):
            for stdout in result.all_stdout.values():
                if 'DER_NO_PERM' not in stdout:
                    self.fail('Expected mount as root to fail without ACLs in single-user mode')

        self.log.info('Verify root cannot mount the container without ACLs in single-user mode')
        self.dfuse.update_params(multi_user=False)
        self.dfuse.run_user = 'root'
        self.dfuse.run(check=False, mount_callback=_check_fail)
        self.dfuse.stop()

        self.log.info('Verify root cannot mount the container without ACLs in multi-user mode')
        self.dfuse.update_params(multi_user=True)
        self.dfuse.run_user = 'root'
        self.dfuse.run(check=False, mount_callback=_check_fail)
        self.dfuse.stop()

        self.log.info('Mounting dfuse in single-user mode')
        self.dfuse.update_params(multi_user=False)
        self.dfuse.run_user = None  # Current user
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
        self.dfuse.run()
        self.dfuse.stop()

        self.log.info('Verify root can mount the container with ACLs in muli-user mode')
        self.dfuse.update_params(multi_user=True)
        self.dfuse.run_user = 'root'
        self.dfuse.run()
        self.dfuse.stop()

        self.log.info('Re-mounting dfuse in multi-user mode')
        self.dfuse.update_params(multi_user=True)
        self.dfuse.run_user = None  # Current user
        self.dfuse.run()

        # Give root permission to create containers in the pool
        pool.update_acl(False, entry="A::root@:rw")

        self.log.info('Verify UNS sub-container creation as root in multi-user mode succeeds')
        # DaosCommand cannot be used directly because this needs to run remotely
        daos_command = self.get_daos_command()
        daos_path = os.path.join(daos_command.command_path, daos_command.command)
        cont_path = os.path.join(root_dir, 'sub_cont')
        first_client = NodeSet(self.hostlist_clients[0])
        command = command_as_user(
            '{} container create --type POSIX --path {}'.format(daos_path, cont_path), 'root')
        if not run_remote(self.log, first_client, command).passed:
            self.fail('Failed to create sub-container as root in multi-user mode')

        # Verify the container is created correctly
        command = '{} container get-prop --path {}'.format(daos_path, cont_path)
        if not run_remote(self.log, first_client, command).passed:
            self.fail('Failed to get sub-container properties in multi-user mode')

        # List dfuse entries
        command = 'ls -l {}'.format(root_dir)
        if not run_remote(self.log, self.hostlist_clients, command).passed:
            self.fail('Failed to {}'.format(command))
