"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from command_utils_base import CommandFailure
from general_utils import get_journalctl, journalctl_time, wait_for_result
from ior_test_base import IorTestBase
from ior_utils import IorCommand
from job_manager_utils import get_job_manager


class ContinuesAfterStop(IorTestBase):
    """Verify rebuild continues after one of the ranks is stopped.

    :avocado: recursive
    """

    def run_ior_basic(self, namespace, pool, container):
        """Run IOR once with configurations in the test yaml.

        Args:
            namespace (str): Namespace that defines block_size and transfer_size.
            pool (TestPool): Pool to use with IOR.
            container (TestContainer): Container to use with IOR.
        """
        ior_cmd = IorCommand(self.test_env.log_dir, namespace=namespace)
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(pool, container.identifier)
        testfile = os.path.join(os.sep, "test_file_1")
        ior_cmd.test_file.update(testfile)
        manager = get_job_manager(test=self, job=ior_cmd, subprocess=self.subprocess)
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
        ppn = self.params.get("ppn", namespace)
        manager.assign_processes(ppn=ppn)

        try:
            manager.run()
        except CommandFailure as error:
            self.fail(f"IOR failed! {error}")

    def test_continuous_after_stop(self):
        """Verify rebuild continues after one of the ranks is stopped.

        1. Create a pool and a container.
        2. Run IOR that takes several seconds.
        3. Stop one of the ranks (rank 3).
        4. Look for the start of the rebuild (Rebuild [scanning]) in journalctl with daos_server
        identifier.
        5. As soon as the message is detected, stop the rest of the ranks (0, 1, 2).
        6. Restart the three ranks.
        7. Wait for rebuild to finish.

        Jira ID: DAOS-6287

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=rebuild
        :avocado: tags=ContinuesAfterStop,test_continuous_after_stop
        """
        self.log_step("Create a pool and a container.")
        pool = self.get_pool()
        container = self.get_container(pool=pool)

        self.log_step("Run IOR that takes several seconds.")
        self.run_ior_basic(namespace="/run/ior/*", pool=pool, container=container)
        ior_start_time = journalctl_time()

        self.log_step("Stop one of the ranks (rank 3).")
        self.server_managers[0].stop_ranks(ranks=[3])

        msg = ("4. Look for the start of the rebuild (Rebuild [scanning]) in journalctl with "
               "daos_server identifier.")
        self.log_step(msg)

        def _search_scanning():
            """Search 'Rebuild [scanning]' from journalctl output using wait_for_result().
            """
            journalctl_out = get_journalctl(
                hosts=self.hostlist_servers, since=ior_start_time, until=None,
                journalctl_type="daos_server")

            for _, journalctl in enumerate(journalctl_out):
                data = journalctl["data"]
                for line in data.splitlines():
                    if "Rebuild [scanning]" in line:
                        self.log.info("'Rebuild [scanning]' found: %s", line)
                        return True
            return False

        scanning_found = wait_for_result(self.log, _search_scanning, timeout=120, delay=1)
        if not scanning_found:
            self.fail("'Rebuild [scanning]' wasn't found in journalctl after stopping a rank!")

        self.log_step("As soon as the message is detected, stop the rest of the ranks (0, 1, 2).")
        self.server_managers[0].stop_ranks(ranks=[0, 1, 2])

        self.log_step("Restart the three ranks.")
        self.server_managers[0].start_ranks(ranks=[0, 1, 2])

        self.log_step("Wait for rebuild to finish.")
        pool.wait_for_rebuild_to_end(interval=5)
