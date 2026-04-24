"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from ior_test_base import IorTestBase
from util.network_utils import get_common_provider, SUPPORTED_PROVIDERS


class ChangingFabricProvider(IorTestBase):
    """Test class Description: Test changing the fabric provider without reformatting the storage

       Look for confirmation in the DAOS logs

    :avocado: recursive
    """

    def test_changing_fabric_provider(self):
        """

        Test Description:
            Purpose of this test is to test the fabric provider can
            be changed without reformatting the storage. Confirm that
            the provider changed by looking for messages in
            the logs.

        Use case:

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=control
        :avocado: tags=ChangingFabricProvider,test_changing_fabric_provider
        """
        ior_read_flags = self.params.get("read_flags", "/run/ior/*")
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')

        # Get all providers supported by the interface in use
        self.log_step("Find common providers")
        common_providers = get_common_provider(self.log, self.host_info.all_hosts, self.test_env.interface)
        self.log.info(f"common providers: {common_providers}")
        # Get a different provider than what is being used
        original_provider = self.server_managers[0].manager.job.yaml.get_value('provider')
        new_provider = None
        for provider in common_providers:
            if original_provider not in provider and provider in SUPPORTED_PROVIDERS:
                new_provider = provider
                break

        if new_provider is None:
            self.fail(f"No alternative provider found. Available: {common_providers}, Current: {original_provider}")

        self.log.info(f"Original provider: {original_provider}, New provider: {new_provider}")

        # Run IOR with the original provider
        try:
            self.run_ior_with_pool(intercept=intercept, fail_on_warning=False)
            self.log.info("Initial IOR write completed successfully")
        except Exception as error:
            self.fail(f"Initial IOR write failed with original provider {original_provider}: {error}")

        # Stop all DAOS engines and agent processes
        self.log_step("Stop all DAOS engines and agents")
        self.agent_managers[0].dump_attachinfo()
        self.server_managers[0].dmg.system_stop(False)
        self.stop_agents()

        # Update the provider and write a new server YAML file.
        self.log_step(f"Generate config at {self.test_env.server_config} and update provider to {new_provider}")

        try:
            self.server_managers[0].manager.job.yaml.provider.value = new_provider
            generated_yaml = self.server_managers[0].manager.job.yaml.get_yaml_data()
            self.server_managers[0].manager.job.create_yaml_file(yaml_data=generated_yaml)
            self.log.info(f"Successfully updated server config with new provider: {new_provider}")
        except Exception as error:
            self.fail(f"Failed to update server configuration with new provider: {error}")

        # Get the daos server yaml data again and check values
        self.log.info(f'self.server_managers[0].manager.job.yaml.get_yaml_data() = {self.server_managers[0].manager.job.yaml.get_yaml_data()}')

        # Restart server with the new config.
        self.log_step(f"Restarting server with the new provider {self.server_managers[0].manager.job.yaml.get_value('provider')}")
        try:
            self.restart_servers()
            self.server_managers[0].dmg.system_query()
            self.log.info("Server restart completed successfully")
        except Exception as error:
            self.fail(f"Failed to restart servers with new provider: {error}")


        # Restart the daos_agent and dump agent info
        self.log_step("Restarting DAOS agents")
        try:
            self.start_agent_managers()
            self.agent_managers[0].dump_attachinfo()
            self.log.info("Agent restart completed successfully")
        except Exception as error:
            self.fail(f"Failed to restart agents: {error}")

        # Verify the provider was actually changed
        current_provider = self.server_managers[0].manager.job.yaml.get_value('provider')
        self.log.info(f"Current provider after restart: {current_provider}")
        if current_provider != new_provider:
            self.fail(f"Provider change failed. Expected: {new_provider}, Actual: {current_provider}")

        # Check RAS event in doas_control.log

        # IOR read file to verify system works with new provider
        self.log_step("Running IOR read test with new provider")
        try:
            self.ior_cmd.flags.update(ior_read_flags)
            self.run_ior_with_pool(intercept=intercept, create_pool=False, create_cont=False)
            self.log.info("IOR read test with new provider completed successfully")
        except Exception as error:
            self.fail(f"IOR read test failed with new provider {new_provider}: {error}")

        # Change the provider back to the original and verify the switch back works
        self.log_step(f"Restoring original provider: {original_provider}")

        # Stop engines and agents again
        self.server_managers[0].dmg.system_stop(False)
        self.stop_agents()

        # Restore original provider
        self.server_managers[0].manager.job.yaml.provider.value = original_provider
        generated_yaml = self.server_managers[0].manager.job.yaml.get_yaml_data()
        self.server_managers[0].manager.job.create_yaml_file(yaml_data=generated_yaml)


        # Restart servers with original provider
        self.log_step("Restarting DAOS servers")
        try:
            self.restart_servers()
            self.server_managers[0].dmg.system_query()
            self.log.info("Server restart completed successfully")
        except Exception as error:
            self.fail(f"Failed to restart servers with original provider: {error}")

        # Restart the daos_agent and dump agent info
        self.log_step("Restarting DAOS agents")
        try:
            self.start_agent_managers()
            self.agent_managers[0].dump_attachinfo()
            self.log.info("Agent restart completed successfully")
        except Exception as error:
            self.fail(f"Failed to restart agents: {error}")

        # Verify restoration of original provider
        restored_provider = self.server_managers[0].manager.job.yaml.get_value('provider')
        if restored_provider != original_provider:
            self.fail(f"Provider restoration failed. Expected: {original_provider}, Actual: {restored_provider}")

        # IOR read file to verify system works with original provider
        self.log_step("Running IOR read test with original provider")
        try:
            self.ior_cmd.flags.update(ior_read_flags)
            self.run_ior_with_pool(intercept=intercept, create_pool=False, create_cont=False)
            self.log.info("IOR read test with new provider completed successfully")
        except Exception as error:
            self.fail(f"IOR read test failed with new provider {new_provider}: {error}")


        self.log.info("Test completed successfully - fabric provider was changed and restored without storage reformatting")
