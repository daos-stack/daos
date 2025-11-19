"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from ior_utils import get_ior
from job_manager_utils import get_job_manager


class RbldInteractive(TestWithServers):
    """Test class for interactive rebuild tests.

    :avocado: recursive
    """

    def test_rebuild_interactive(self):
        """
        Use Cases:
            Pool rebuild with interactive start/stop. 

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,pool
        :avocado: tags=RbldInteractive,test_rebuild_interactive
        """
        self.log_step("Setup pool")
        pool = self.get_pool(connect=False)

        # TODO - For debugging
        do_rebuild = True


        # Collect server configuration information
        server_count = len(self.hostlist_servers)
        engines_per_host = int(self.server_managers[0].get_config_value("engines_per_host") or 1)
        targets_per_engine = int(self.server_managers[0].get_config_value("targets"))
        self.log.info(
            "Running with %s servers, %s engines per server, and %s targets per engine",
            server_count, engines_per_host, targets_per_engine)

        self.log_step(f"Verify pool state before rebuild")
        # TODO verify pool global version, enabled and disabled ranks
        status = pool.check_pool_info(
            pi_nnodes=server_count * engines_per_host,
            pi_ntargets=server_count * engines_per_host * targets_per_engine,
            pi_ndisabled=0)
        status &= pool.check_rebuild_status(rs_state=1, rs_obj_nr=0, rs_rec_nr=0, rs_errno=0)
        if not status:
            # TODO better error message
            self.fail("Invalid pool state before rebuild")

        self.log_step(f"Create container and run IOR")
        cont_ior = self.get_container(pool, namespace="/run/cont_ior/*")
        ior_flags_write = self.params.get("flags_write", '/run/ior/*')
        ior_flags_read = self.params.get("flags_read", '/run/ior/*')
        ior_ppn = self.params.get("ppn", '/run/ior/*')


        job_manager = get_job_manager(self, subprocess=False)
        ior = get_ior(
            self, job_manager, self.hostlist_clients, self.workdir, None, namespace='/run/ior/*')
        ior.manager.job.update_params(flags=ior_flags_write, dfs_oclass=cont_ior.oclass.value)
        ior.run(cont_ior.pool, cont_ior, None, ior_ppn, display_space=False)

        if do_rebuild:
            ranks_to_exclude = self.random.sample(list(self.server_managers[0].ranks.keys()), k=2)
            self.log_step(f"Exclude random rank {ranks_to_exclude}")
            # TODO need to test exclude and drain, etc.
            pool.exclude(ranks_to_exclude)

            self.log_step("Wait for rebuild to start")
            pool.wait_for_rebuild_to_start(interval=1)

            self.log_step("Manually stop rebuild")
            pool.rebuild_stop(force=True)

            self.log_step("Wait for rebuild to fail")
            pool.wait_for_rebuild_to_fail(interval=3)

            self.log_step("Verify pool info after rebuild stopped")
            status = pool.check_pool_info(
                pi_nnodes=server_count * engines_per_host,
                pi_ntargets=server_count * engines_per_host * targets_per_engine,
                pi_ndisabled=targets_per_engine
            )
            status &= pool.check_rebuild_status(
                rs_state=2, rs_errno=-2027)  # TODO what should the state be?
            if not status:
                # TODO better error message
                self.fail("Invalid pool state after rebuild stopped")

        self.log_step("Verify IOR after rebuild stopped")
        ior.manager.job.update_params(flags=ior_flags_read)
        ior.run(cont_ior.pool, cont_ior, None, ior_ppn, display_space=False)

        # TODO some other steps from test plan
        

        self.log_step("Test Passed")
