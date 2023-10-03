'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

import yaml

from apricot import TestWithServers
from server_utils import ServerFailed


class ConfigGenerateRun(TestWithServers):
    """JIRA ID: DAOS-5986

    Verify dmg config generate by running daos_server with generated config
    file.

    :avocado: recursive
    """

    def test_config_generate_run(self):
        """Run daos_server with generated server config file.

        1. Start daos_server.
        2. Call dmg config generate with different parameters.
        3. Store the generated output to a temporary directory - self.test_dir
        4. Copy the generated output from the temp dir to /etc/daos of the server node.
        5. Stop daos_server.
        6. Restart daos_server.

        See yaml for the test cases.

        Note: When running locally, use 50 sec timeout in DaosServerCommand.__init__()

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=control,dmg_config_generate
        :avocado: tags=ConfigGenerateRun,test_config_generate_run
        """
        num_engines = self.params.get("num_engines", "/run/config_generate_params/*/")
        scm_only = self.params.get("scm_only", "/run/config_generate_params/*/")
        net_class = self.params.get("net_class", "/run/config_generate_params/*/")
        net_provider = self.params.get("net_provider", "/run/config_generate_params/*/")
        use_tmpfs_scm = self.params.get("use_tmpfs_scm", "/run/config_generate_params/*/")

        # use_tmpfs_scm specifies that a MD-on-SSD conf should be generated and control metadata
        # path needs to be set in that case.
        ext_md_path = ""
        if use_tmpfs_scm:
            ext_md_path = self.test_dir

        # Call dmg config generate. AP is always the first server host.
        server_host = self.hostlist_servers[0]
        result = self.get_dmg_command().config_generate(
            access_points=server_host, num_engines=num_engines, scm_only=scm_only,
            net_class=net_class, net_provider=net_provider, use_tmpfs_scm=use_tmpfs_scm,
            control_metadata_path=ext_md_path)

        try:
            generated_yaml = yaml.safe_load(result.stdout)
        except yaml.YAMLError as error:
            self.fail(f"Error loading dmg generated config! {error}")

        # Stop and restart daos_server. self.start_server_managers() has the
        # server startup check built into it, so if there's something wrong,
        # it'll throw an error.
        self.log.info("Stopping servers")
        self.stop_servers()

        # Create a new server config from generated_yaml and update SCM-related
        # data in engine_params so that the cleanup before the server start
        # works.
        self.log.info("Copy config to /etc/daos and update engine_params")
        self.server_managers[0].update_config_file_from_file(generated_yaml)

        # Start server with the generated config.
        self.log.info("Restarting server with the generated config")
        try:
            agent_force = self.start_server_managers(force=True)
        except ServerFailed as error:
            self.fail(f"Restarting server failed! {error}")

        # We don't need agent for this test. However, when we stop the server,
        # agent is also stopped. Then the harness checks that the agent is
        # running during the teardown. If agent isn't running at that point, it
        # would cause an error, so start it here.
        self.log.info("Restarting agents")
        self.start_agent_managers(force=agent_force)
