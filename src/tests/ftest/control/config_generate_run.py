#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import yaml

from apricot import TestWithServers
from command_utils import CommandFailure
from general_utils import get_default_config_file, distribute_files,\
    DaosTestError
from server_utils import ServerFailed
from server_utils_params import DaosServerYamlParameters


class ConfigGenerateRun(TestWithServers):
    """Test ID: DAOS-5986

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
        generated_yaml = self.get_dmg_command().config_generate(
            access_points=server_host, num_engines=num_engines,
            min_ssds=min_ssds, net_class=net_class)

        # Create a temporary file in self.test_dir and write the generated
        # config.
        temp_file_path = os.path.join(self.test_dir, "temp_server.yml")
        try:
            with open(temp_file_path, 'w') as write_file:
                yaml.dump(generated_yaml, write_file, default_flow_style=False)
        except Exception as error:
            raise CommandFailure(
                "Error writing the yaml file! {}: {}".format(
                    temp_file_path, error)) from error

        # Copy the config from temp dir to /etc/daos of the server node.
        default_server_config = get_default_config_file("server")
        try:
            distribute_files(
                self.hostlist_servers, temp_file_path, default_server_config,
                verbose=False, sudo=True)
        except DaosTestError as error:
            raise CommandFailure(
                "ERROR: Copying yaml configuration file to {}: "
                "{}".format(server_host, error)) from error

        # Stop and restart daos_server. self.start_server_managers() has the
        # server startup check built into it, so if there's something wrong,
        # it'll throw an error.
        self.log.debug("## Stopping servers")
        self.stop_servers()

        # We don't need agent for this test. However, when we stop the server,
        # agent is also stopped. Then the harness checks that the agent is
        # running during the teardown. If agent isn't running at that point, it
        # would cause an error, so start it here.
        self.log.debug("## Restarting agents")
        self.start_agent_managers()

        # Before restarting daos_server, we need to clear SCM. Unmount the mount
        # point, wipefs the disks, etc. This clearing step is built into the
        # server start steps. It'll look at the engine_params of the
        # server_manager and clear the SCM set there, so we need to overwrite it
        # before starting. Set the values from the generated config.
        self.log.debug("## Resetting engine_params")
        self.server_managers[0].manager.job.yaml.engine_params = []
        engines = generated_yaml["engines"]
        for i, engine in enumerate(engines):
            self.log.debug("## engine {}".format(i))
            per_engine_yaml_parameters =\
                DaosServerYamlParameters.PerEngineYamlParameters(i)
            self.log.debug("## scm_mount = {}".format(engine["scm_mount"]))
            per_engine_yaml_parameters.scm_mount.update(engine["scm_mount"])
            self.log.debug("## scm_class = {}".format(engine["scm_class"]))
            per_engine_yaml_parameters.scm_class.update(engine["scm_class"])
            per_engine_yaml_parameters.scm_size.update(None)
            self.log.debug("## scm_list = {}".format(engine["scm_list"]))
            per_engine_yaml_parameters.scm_list.update(engine["scm_list"])
            self.server_managers[0].manager.job.yaml.engine_params.append(
                per_engine_yaml_parameters)

        # Start server with the generated config.
        self.log.debug("## Restarting servers")
        try:
            self.start_server_managers()
        except ServerFailed as error:
            self.log.info("## Restarting server failed! %s", error)
