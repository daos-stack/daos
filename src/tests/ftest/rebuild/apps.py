"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse, stop_dfuse
from e3sm_io import E3SMIO_INPUTS, run_e3sm_io
from job_manager_utils import get_job_manager


class RbldApps(TestWithServers):
    """Test class for rebuild with various apps.

    :avocado: recursive
    """

    def test_rebuild_apps(self):
        """Rebuild with various apps

        Apps Tested:
            E3SM-IO

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,e3sm_io
        :avocado: tags=RbldApps,test_rebuild_apps
        """
        e3sm_io_processes = self.params.get("np", '/run/e3sm_io/*')

        self.log_step("Setup pool and container")
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        self.log_step("Mount dfuse")
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        self.log_step("Run e3sm_io")
        job_manager = get_job_manager(self, subprocess=False, timeout=self.get_remaining_time())
        run_e3sm_io(
            self, job_manager, self.hostlist_clients, self.workdir, None, e3sm_io_processes,
            working_dir=dfuse.mount_dir.value,
            e3sm_io_params={
                'input_file': E3SMIO_INPUTS['e3sm_io_map_i_case_1344p.h5']})

        rank = self.random.choice(list(self.server_managers[0].ranks.keys()))
        self.log_step(f"Exclude 1 crandom rank {rank}")
        self.server_managers[0].stop_ranks([rank], True)

        self.log_step("Wait for rebuild to start")
        pool.wait_for_rebuild_to_start(interval=3)

        self.log_step("Wait for rebuild to end")
        pool.wait_for_rebuild_to_end(interval=3)

        self.log_step("Stop dfuse")
        stop_dfuse(self, dfuse)

        self.log_step("Test Passed")
