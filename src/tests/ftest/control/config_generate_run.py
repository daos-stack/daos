'''
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

import os

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
        control_metadata = None
        if use_tmpfs_scm:
            control_metadata = os.path.join(self.test_env.log_dir, 'control_metadata')

        # Call dmg config generate. AP is always the first server host.
        self.log_step("Generating server configuration")
        server_host = self.hostlist_servers[0]
        result = self.get_dmg_command().config_generate(
            mgmt_svc_replicas=server_host, num_engines=num_engines, scm_only=scm_only,
            net_class=net_class, net_provider=net_provider, use_tmpfs_scm=use_tmpfs_scm,
            control_metadata_path=control_metadata)

        try:
            generated_yaml = yaml.safe_load(result.stdout)
        except yaml.YAMLError as error:
            self.fail(f"Error loading dmg generated config! {error}")

        # Stop and restart daos_server. self.start_server_managers() has the
        # server start-up check built into it, so if there's something wrong,
        # it'll throw an error.
        self.log_step("Stopping servers")
        self.stop_servers()

        # Create a new server config from generated_yaml and update SCM-related
        # data in engine_params so that the cleanup before the server start
        # works.
        self.log_step(f"Copy config to {self.test_env.server_config} and update engine_params")
        self.server_managers[0].update_config_file_from_file(generated_yaml)

        # Start server with the generated config.
        self.log_step("Restarting server with the generated config")
        try:
            self.start_server_managers(force=True)
        except ServerFailed as error:
            self.fail(f"Restarting server failed! {error}")

        self.log.info("Test passed")
