"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re

from ior_test_base import IorTestBase
from util.network_utils import SUPPORTED_PROVIDERS, get_common_provider


class ChangingFabricProvider(IorTestBase):
    """
    Test class Description:
        Test class for changing the fabric provider without reformatting the storage feature

    :avocado: recursive
    """

    def _scan_control_log_for_uri(self, provider):
        """Scan DAOS control logs for lines with URI and the input provider.

        Args:
            provider (str): the provider to search for in the control files

        Returns:
            line_count(int): number of lines that contain URI and provider
        """

        self.log_step("Scanning DAOS control logs for URI lines")

        line_count = 0
        txt = f"URI to {provider}".replace('+', '\+')
        result = self.server_managers[0].search_control_logs(txt)
        for data in result.output:
            self.log.info("URI scan output for %s:", data.hosts)
            if data.stdout:
                for line in data.stdout:
                    self.log.info("%s", line)
                    line_count += 1
            else:
                self.log.info("No URI lines found")

        return line_count

    def _parse_attachinfo_output(self, provider):
        """Parse the output of the dump_attachinfo() method and check that the client
        is using the specified provider and all ranks are using the specified provider.

        Args:
            provider (str): provider name to count in the output

        Returns:
            bool: True if the client and all ranks are using the specified provider, False if the client is not using
            the specified provider or some ranks are not using it.

        """
        attachinfo_output = self.agent_managers[0].dump_attachinfo()

        client_ok = False
        ranks_ok = True
        for line in attachinfo_output.joined_stdout.splitlines():
            line = line.strip()
            if "client:provider:" in line:
                client_ok = provider in line
            elif re.match(r"^\d+\s+\S+://", line) and provider not in line:
                ranks_ok = False
        return client_ok and ranks_ok

    def _get_new_provider(self, current_provider):
        """Get a supported provider that differs from the active provider.

        Args:
            current_provider (str): currently configured provider

        Returns:
            str: selected alternative provider or None if not found
        """
        # Get all supported providers for the interface in use
        self.log_step("Choose a new common provider")
        common_providers = get_common_provider(self.log,
                                               self.host_info.all_hosts, self.test_env.interface)

        # Find a different provider than the input provider
        new_provider = None
        for provider in common_providers:
            if current_provider not in provider and provider in SUPPORTED_PROVIDERS:
                new_provider = provider
                break

        if new_provider is None:
            self.fail(f'No alternative provider found; available: {common_providers}, '
                      f'current: {current_provider}')

        return new_provider

    def _update_server_yaml(self, provider):
        """Update the server yaml provider and write the updated yaml file.

        Args:
            provider (str): provider to set in server yaml
        """
        # Update the provider and write a new server YAML file.
        self.log_step(f'Generate server configuration file with provider {provider}')

        try:
            self.server_managers[0].manager.job.yaml.provider.value = provider
            generated_yaml = self.server_managers[0].manager.job.yaml.get_yaml_data()
            self.server_managers[0].manager.job.create_yaml_file(yaml_data=generated_yaml)
        except Exception as error:
            self.fail(f"Failed to update server YAML with provider {provider}: {error}")

        self.log.info(f"Successfully updated server config with provider {provider}")

    def _restart_servers_and_agents(self, provider):
        """Restart servers and agents after a provider config change.

        Args:
            provider (str): provider label for log/failure messages
        """
        self.log_step(f"Restart DAOS server with provider {provider}")
        try:
            self.restart_servers()
        except CommandFailure as error:
            self.fail(f"Failed to restart servers with provider {provider}: {error}")

        self.log.info("Server restart completed successfully")

        self.log_step(f"Restart DAOS agents with provider {provider}")
        try:
            self.start_agent_managers()
        except CommandFailure as error:
            self.fail(f"Failed to restart agents with provider {provider}: {error}")

        self.log.info("Agent restart completed successfully")

    def test_change_fabric_provider(self):
        """

        Test Description:
            Test # 1 from the Changing Fabric Provider test plan
            Verify that the fabric provider can be changed without
            reformatting the storage by reading from an existing
            file after the fabric provider is changed.

        Use case:

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=control
        :avocado: tags=ChangingFabricProvider,test_change_fabric_provider
        """
        ior_read_flags = self.params.get("read_flags", "/run/ior/*")
        intercept = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')

        # Get a supported provider different from the active provider
        original_provider = self.server_managers[0].manager.job.yaml.get_value('provider')
        new_provider = self._get_new_provider(original_provider)

        self.log.info(f"Original provider: {original_provider}, New provider: {new_provider}")

        # Write to a file using IOR
        self.log_step(f'Write with IOR with original provider {original_provider}')
        try:
            self.run_ior_with_pool(intercept=intercept, fail_on_warning=False)
        except Exception as error:
            self.fail(f'IOR write failed with original provider {original_provider}: {error}')

        self.log.info(f'Initial IOR write with provider '
                      f'{original_provider} completed successfully')

        # Work around for pool.connect() issue
        self.pool.disconnect()

        # Stop all DAOS engines and agent processes
        self.log_step("Stop all DAOS engines and agents")
        self.server_managers[0].dmg.system_stop(force=True)
        self.stop_agents()

        # Update the provider and write a new server YAML file.
        self._update_server_yaml(new_provider)

        # Restart the DAOS servers and agents with the new config.
        self._restart_servers_and_agents(new_provider)

        # Verify the provider was changed
        current_provider = self.server_managers[0].manager.job.yaml.get_value('provider')
        if current_provider != new_provider:
            self.fail(f'Provider change failed: expected {new_provider}, '
                      f'actual {current_provider}')

        # Look for the new provider in the agent dump_attachinfo output
        self.log_step(f"Verify servers and client are using provider {new_provider}")

        if self._parse_attachinfo_output(new_provider):
            self.log.info(f"DAOS agent and servers using provider {new_provider}")
        else:
            self.fail(f"DAOS agent or server not using provider {new_provider}")

        # Check provider in DAOS control log
        line_count = self._scan_control_log_for_uri(new_provider)
        if self.server_managers[0].engines != line_count:
            self.fail(f"Only {line_count} DAOS server using provider {new_provider}")

        # Read file with IOR to verify system works with new provider
        self.log_step("Run IOR read test with new provider")
        try:
            self.ior_cmd.flags.update(ior_read_flags)
            self.run_ior_with_pool(intercept=intercept, create_pool=False, create_cont=False)
        except Exception as error:
            self.fail(f"IOR read test failed with new provider: {error}")

        self.log.info("IOR read with new provider completed successfully")


    def test_change_fabric_provider_and_revert(self):
        """

        Test Description:
            Test # 3 from the Changing Fabric Provider test plan
            Verify fabric provider change without storage reformat
            and revert back to the original provider.

        Use case:

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=control
        :avocado: tags=ChangingFabricProvider,test_change_fabric_provider_and_revert
        """
        ior_read_flags = self.params.get("read_flags", "/run/ior/*")
        intercept = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')

        # Get a supported provider different from the active provider
        original_provider = self.server_managers[0].manager.job.yaml.get_value('provider')
        new_provider = self._get_new_provider(original_provider)

        self.log.info(f"Original provider: {original_provider}, New provider: {new_provider}")

        # Run IOR with the original provider
        self.log_step(f'Run IOR with original provider {original_provider}')
        try:
            self.run_ior_with_pool(intercept=intercept, fail_on_warning=False)
        except Exception as error:
            self.fail(f'IOR write failed with original provider {original_provider}: {error}')

        self.log.info(f'IOR write with provider '
                      f'{original_provider} completed successfully')

        # Stop all DAOS engines and agent processes
        self.log_step("Stop all DAOS engines and agents")
        self.server_managers[0].dmg.system_stop(force=True)
        self.stop_agents()

        # Update the provider and write a new server YAML file.
        self._update_server_yaml(new_provider)

        # Restart the DAOS servers and agents with the new config.
        self._restart_servers_and_agents(new_provider)

        # Verify the provider was changed
        current_provider = self.server_managers[0].manager.job.yaml.get_value('provider')
        if current_provider != new_provider:
            self.fail(f'Provider change failed: expected {new_provider}, '
                      f'actual {current_provider}')

        # Look for the new provider in the agent dump_attachinfo
        self.log_step(f"Verify servers and client are using provider {new_provider}")

        if self._parse_attachinfo_output(new_provider):
            self.log.info(f"DAOS agent and servers using provider {new_provider}")
        else:
            self.fail(f"DAOS agent or server not using provider {new_provider}")

        # Check provider in DAOS control log
        line_count = self._scan_control_log_for_uri(new_provider)
        if self.server_managers[0].engines != line_count:
            self.fail(f"Only {line_count} DAOS server using provider {new_provider}")

        # IOR read file to verify system works with new provider
        self.log_step("Running IOR read test with new provider")
        try:
            self.ior_cmd.flags.update(ior_read_flags)
            self.run_ior_with_pool(intercept=intercept, create_pool=False, create_cont=False)
        except Exception as error:
            self.fail(f"IOR read failed with new provider {new_provider}: {error}")

        self.log.info("IOR read with new provider completed successfully")

        # Change the provider back to the original and verify the switch back works
        self.log_step(f"Restoring original provider: {original_provider}")

        # Stop engines and agents again
        self.server_managers[0].dmg.system_stop(force=True)
        self.stop_agents()

        # Update the server YAML file with the original provider
        self._update_server_yaml(original_provider)

        # Restart the DAOS servers and agents with the new config.
        self._restart_servers_and_agents(original_provider)

        # Verify agent and servers are using the original provider
        self.log_step(f"Verify servers and client are using provider {original_provider}")

        if self._parse_attachinfo_output(original_provider):
            self.log.info(f"DAOS agent and servers using provider {original_provider}")
        else:
            self.fail(f"DAOS agent or server not using provider {original_provider}")

        # Check provider in DAOS control log. There should be 2 * number of engines
        line_count = self._scan_control_log_for_uri(original_provider)
        if line_count != 2 * self.server_managers[0].engines:
            self.fail(f"Some DAOS servers not using provider {original_provider}")

        # IOR read file to verify system works with original provider
        self.log_step("Running IOR read test with original provider")
        try:
            self.ior_cmd.flags.update(ior_read_flags)
            self.run_ior_with_pool(intercept=intercept, create_pool=False, create_cont=False)
        except Exception as error:
            self.fail(f"IOR read failed with original provider {original_provider}: {error}")

        self.log.info("IOR read with original provider completed successfully")
