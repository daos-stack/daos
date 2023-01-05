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

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_mu
        :avocado: tags=DfuseMUMount,test_dfuse_mu_mount
        """
        # Create a pool and container for dfuse
        pool = self.get_pool(connect=False)
        cont = self.get_container(pool, label='root_cont')

        # Start dfuse in single-user mode
        self.load_dfuse(self.hostlist_clients)
        self.dfuse.update_params(multi_user=False)
        self.start_dfuse(self.hostlist_clients, pool=pool, cont=cont)

        root_dir = self.dfuse.mount_dir.value

        # stat as dfuse user in single-user mode should succeed
        command = 'stat {}'.format(root_dir)
        if not run_remote(self.log, self.hostlist_clients, command).passed:
            self.fail('Failed to stat in single-user mode')

        # stat as root in single-user mode should fail
        command = command_as_user('stat {}'.format(root_dir), 'root')
        if run_remote(self.log, self.hostlist_clients, command).passed:
            self.fail('Expected stat to fail as root in single-user mode')

        # Stop dfuse and mount in multi-user mode
        self.dfuse.stop()
        self.dfuse.update_params(multi_user=True)
        self.start_dfuse(self.hostlist_clients, pool=pool, cont=cont)

        # stat as dfuse user in multi-user mode should succeed
        command = 'stat {}'.format(root_dir)
        if not run_remote(self.log, self.hostlist_clients, command).passed:
            self.fail('Failed to stat in multi-user mode')

        # stat as root in multi-user mode should succeed
        command = command_as_user('stat {}'.format(root_dir), 'root')
        if not run_remote(self.log, self.hostlist_clients, command).passed:
            self.fail('Failed to stat as root in multi-user mode')

        # Give root RW access
        pool.update_acl(False, entry="A::root@:rw")

        # Create a sub-container as root, with a UNS path in dfuse.
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
