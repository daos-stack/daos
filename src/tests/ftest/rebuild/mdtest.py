"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
import time
from datetime import datetime

from general_utils import get_subprocess_stdout
from mdtest_test_base import MdtestBase


class RebuildMdtest(MdtestBase):
    """Rebuild test cases with Mdtest.

    :avocado: recursive
    """

    def test_rebuild_mdtest(self):
        """Jira ID: DAOS-15197.

        Test Description: Trigger rebuild during read of many small files.
                          I/O performed using Mdtest.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool,rebuild
        :avocado: tags=RebuildMdtest,test_rebuild_mdtest
        """
        targets = self.server_managers[0].get_config_value("targets")
        create_flags = self.params.get("create_flags", "/run/mdtest/*")
        read_flags = self.params.get("read_flags", "/run/mdtest/*")
        self.processes = self.params.get("np", "/run/mdtest/*", self.processes)
        rebuild_check_inverval = self.params.get("rebuild_check_inverval", "/run/pool/*")

        # Time various operations for debugging and tuning
        times = {}

        self.log_step("Setup pool and container")
        self.pool = self.get_pool()
        self.container = self.get_container(self.pool)

        self.log_step("Create files with mdtest")
        self.mdtest_cmd.update_params(flags=create_flags)
        job_manager = self.get_mdtest_job_manager_command(self.manager)
        self.execute_mdtest(display_space=False, job_manager=job_manager)

        self.log_step("Verify pool state before rebuild")
        pool_checks = {
            "pi_nnodes": self.server_managers[0].engines,
            "pi_ntargets": self.server_managers[0].engines * targets,
            "pi_ndisabled": 0,
        }
        self.assertTrue(
            self.pool.check_pool_info(**pool_checks),
            "Unexpected pool state before rebuild")

        self.log_step("Verify rebuild status before rebuild")
        self.assertTrue(
            self.pool.check_rebuild_status(rs_errno=0, rs_state=1, rs_obj_nr=0, rs_rec_nr=0),
            "Unexpected pool rebuild status before rebuild")

        self.log_step("Read files with mdtest and kill 1 rank during read")
        times["read_start"] = datetime.now()
        self.mdtest_cmd.update_params(flags=read_flags)
        self.subprocess = True  # run mdtest in the background
        job_manager = self.get_mdtest_job_manager_command(self.manager)
        self.execute_mdtest(display_space=False, job_manager=job_manager)
        self.log.info("Sleeping 5 seconds for mdtest to start")
        time.sleep(5)

        self.log_step("Kill 1 random rank")
        times["kill_rank"] = datetime.now()
        random_ranks = self.server_managers[0].get_random_ranks(1)
        self.server_managers[0].stop_ranks(random_ranks, self.d_log, force=True)

        self.server_managers[0].stop_random_rank(self.d_log, force=True)

        self.log_step("Wait for rebuild to start")
        self.pool.wait_for_rebuild_to_start(interval=rebuild_check_inverval)
        times["rebuild_start"] = datetime.now()

        self.log_step("Wait for rebuild to end")
        self.pool.wait_for_rebuild_to_end(interval=rebuild_check_inverval)
        times["rebuild_end"] = datetime.now()

        self.log_step("Verify pool state after rebuild")
        pool_checks["pi_ndisabled"] = targets
        self.assertTrue(
            self.pool.check_pool_info(**pool_checks),
            "Unexpected pool state after rebuild")

        self.log_step("Verify rebuild status after rebuild")
        self.assertTrue(
            self.pool.check_rebuild_status(rs_errno=0, rs_state=2),
            "Unexpected pool rebuild status after rebuild")

        self.log_step("Verify mdtest completed successfully")
        mdtest_returncode = job_manager.process.wait()
        if mdtest_returncode != 0:
            self.fail("mdtest read during rebuild failed")
        mdtest_output = get_subprocess_stdout(job_manager.process)
        # Parse timestamp format "mm/dd/yyyy hh:mm:ss"
        mdtest_finish_time = re.findall(
            r'finished at ([0-9]{2}/[0-9]{2}/[0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2})', mdtest_output)
        if not mdtest_finish_time:
            self.fail("failed to parse mdtest finish timestamp")
        times["read_end"] = datetime.strptime(mdtest_finish_time[0], '%m/%d/%Y %H:%M:%S')

        # Print times for debugging and tuning
        # Convert all to a readable format
        max_key_len = max(map(len, times.keys()))
        for key, val in times.items():
            formatted_key = f"{key}:".ljust(max_key_len + 1)
            formatted_time = val.strftime("%Y-%m-%d %H:%M:%S")
            self.log.info('%s %s', formatted_key, formatted_time)
