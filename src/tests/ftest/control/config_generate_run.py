#!/usr/bin/python3
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import yaml

from apricot import TestWithServers
from exception_utils import CommandFailure
from server_utils import ServerFailed
from server_utils_params import DaosServerYamlParameters


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
        4. Copy the generated output from the temp dir to /etc/daos of the
        server node.
        5. Stop daos_server.
        6. Restart daos_server.

        See yaml for the test cases.

        Note: When running locally, use 50 sec timeout in
        DaosServerCommand.__init__()

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,config_generate_entries,config_generate_run
        """
        num_engines = self.params.get(
            "num_engines", "/run/config_generate_params/*/")
        min_ssds = self.params.get(
            "min_ssds", "/run/config_generate_params/*/")
        net_class = self.params.get(
            "net_class", "/run/config_generate_params/*/")

        # Call dmg config generate. AP is always the first server host.
        server_host = self.hostlist_servers[0]
        result = self.get_dmg_command().config_generate(
            access_points=server_host, num_engines=num_engines,
            min_ssds=min_ssds, net_class=net_class)

        try:
            generated_yaml = yaml.safe_load(result.stdout)
        except yaml.YAMLError as error:
            raise CommandFailure("Error loading dmg generated config!")

        # Stop and restart daos_server. self.start_server_managers() has the
        # server startup check built into it, so if there's something wrong,
        # it'll throw an error.
        self.log.info("Stopping servers")
        self.stop_servers()

        # Create a new server config from generated_yaml and update SCM-related
        # data in engine_params so that the cleanup before the server start
        # works.
        self.log.info("Copy config to /etc/daos and update engine_params")
        self.server_managers[0].update_config_file_from_file(
            self.hostlist_servers, self.test_dir, generated_yaml)

        # Start server with the generated config.
        self.log.info("Restarting server with the generated config")
        try:
            agent_force = self.start_server_managers(force=True)
        except ServerFailed as error:
            self.fail("Restarting server failed! {}".format(error))

        # We don't need agent for this test. However, when we stop the server,
        # agent is also stopped. Then the harness checks that the agent is
        # running during the teardown. If agent isn't running at that point, it
        # would cause an error, so start it here.
        self.log.info("Restarting agents")
        self.start_agent_managers(force=agent_force)
