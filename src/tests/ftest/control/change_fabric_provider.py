"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re

from exception_utils import CommandFailure
from ior_test_base import IorTestBase
from util.network_utils import SUPPORTED_PROVIDERS, get_common_provider


class ChangeFabricProvider(IorTestBase):
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
        txt = f"URI to {provider}".replace('+', '\\+')
        result = self.server_managers[0].search_control_logs(txt)
        for data in result.output:
            if data.stdout:
                for line in data.stdout:
                    line_count += 1
            else:
                self.log.info("No URI lines found")

        return line_count

    def _parse_attachinfo(self, provider):
        """Parse the output of the dump_attachinfo() method and count the number of
        clients and engines using the specified provider.

        Args:
            provider (str): provider name to search for in the output

        Returns:
            tuple: number of clients (int) and number of engines (int) using provider

        """
        num_clients = 0
        num_engines = 0
        attachinfo_output = self.agent_managers[0].dump_attachinfo()

        # Every DAOS client should have one line with "client:provider"
        for line in attachinfo_output.joined_stdout.splitlines():
            if "client:provider:" in line:
                num_clients += 1
            # Every client will have a list of engines and what provider they are using
            # Check this for one client
            # The lines look like: rank provider://ip address
            elif re.match(r"^\d+\s+\S+://", line) and provider in line and num_clients == 1:
                num_engines += 1

        return num_clients, num_engines

    def _get_new_provider(self, current_provider):
        """Get a supported provider that differs from the active provider.

        Args:
            current_provider (str): currently configured provider

        Returns:
            str: selected alternative provider or None if not found
        """
        # Get all supported providers for the interface in use
        self.log_step("Choose a new common provider")
        common_providers = get_common_provider(
            self.log, self.host_info.all_hosts, self.test_env.interface)

        # Find a different provider than the input provider
        alternate_providers = (
            set(SUPPORTED_PROVIDERS)
            .intersection(common_providers)
            .difference(set([current_provider]))
        )

        if not alternate_providers:
            self.fail("No alternative provider found; available: %s current: %s",
                      common_providers, current_provider)

        # Return a random provider
        return self.random.choice(list(alternate_providers))

    def _update_server_yaml(self, provider):
        """Update the server yaml provider and write the updated yaml file.

        Args:
            provider (str): provider to set in server yaml
        """
        # Update the provider and write a new server YAML file.
        self.log_step(f"Generate server configuration file with provider {provider}")

        self.server_managers[0].manager.job.yaml.provider.value = provider
        generated_yaml = self.server_managers[0].manager.job.yaml.get_yaml_data()
        self.server_managers[0].manager.job.create_yaml_file(yaml_data=generated_yaml)

    def _restart_servers_and_agents(self, provider):
        """Restart servers and agents after a provider config change.

        Args:
            provider (str): provider label for log messages
        """
        self.log_step(f"Restart DAOS server with provider {provider}")
        self.restart_servers()

        self.log_step(f"Restart DAOS agents with provider {provider}")
        self.start_agent_managers()

    def _ior_write(self, intercept, provider):
        """Write a file using IOR

        Args:
            intercept (str): intercept method to use or None
            provider (str): provider label for log messages
        """
        self.log_step(f"IOR write with provider {provider}")

        try:
            self.run_ior_with_pool(intercept=intercept, fail_on_warning=False)
        except CommandFailure as error:
            self.fail("IOR write failed: %s", str(error))

        self.log.info("IOR write completed successfully with provider %s", provider)

    def _ior_read(self, intercept, provider, ior_read_flags):
        """Read an existing file using IOR

        Args:
            intercept (str): intercept method to use or None
            provider (str): provider label for log messages
            ior_read_flags (str): IOR command line parameters
        """
        self.log_step(f"Run IOR read test with provider {provider}")

        try:
            self.ior_cmd.flags.update(ior_read_flags)
            self.run_ior_with_pool(intercept=intercept, create_pool=False, create_cont=False)
        except CommandFailure as error:
            self.fail("IOR read failed: %s", str(error))

        self.log.info("IOR read completed successfully with provider %s", provider)

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
        :avocado: tags=ChangeFabricProvider,test_change_fabric_provider
        """
        ior_read_flags = self.params.get("read_flags", "/run/ior/*")
        intercept = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')

        # Get a supported provider different from the active provider
        original_provider = self.server_managers[0].manager.job.yaml.get_value('provider')
        new_provider = self._get_new_provider(original_provider)

        self.log.info("Original provider: %s, New provider: %s", original_provider, new_provider)

        # Write to a file using IOR
        self._ior_write(intercept, original_provider)

        # Stop all DAOS engines and agent processes
        self.log_step("Stop all DAOS engines and agents")
        self.server_managers[0].dmg.system_stop(force=True)
        self.stop_agents()

        # Update the provider and write a new server YAML file.
        self._update_server_yaml(new_provider)

        # Restart the DAOS servers and agents with the new config.
        self._restart_servers_and_agents(new_provider)

        # Look for the new provider in the agent dump_attachinfo output
        self.log_step("Verify servers and clients are using the new provider")

        clients_using_provider, engines_using_provider = self._parse_attachinfo(new_provider)

        if clients_using_provider != len(self.hostlist_clients):
            self.fail("Some DAOS clients not using provider %s; want %d found %d",
                      new_provider, len(self.hostlist_clients), clients_using_provider)
        if engines_using_provider != self.server_managers[0].engines:
            self.fail("Some DAOS engines not using provider %s; want %d found %d",
                      new_provider, self.server_managers[0].engines, engines_using_provider)

        self.log.info("All DAOS engines and clients using provider %s", new_provider)

        # Check provider in DAOS control log
        line_count = self._scan_control_log_for_uri(new_provider)
        if self.server_managers[0].engines != line_count:
            self.fail(f"Only {line_count} DAOS server using provider {new_provider}")

        self.log.info("Provider change registered in DAOS control log for all engines.")

        # Read file with IOR to verify system works with new provider
        self._ior_read(intercept, new_provider, ior_read_flags)

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
        :avocado: tags=ChangeFabricProvider,test_change_fabric_provider_and_revert
        """
        ior_read_flags = self.params.get("read_flags", "/run/ior/*")
        intercept = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')

        # Get a supported provider different from the active provider
        original_provider = self.server_managers[0].manager.job.yaml.get_value('provider')
        new_provider = self._get_new_provider(original_provider)

        self.log.info("Original provider: %s, New provider: %s", original_provider, new_provider)

        # Write to a file using IOR with the original provider
        self._ior_write(intercept, original_provider)

        # Stop all DAOS engines and agent processes
        self.log_step("Stop all DAOS engines and agents")
        self.server_managers[0].dmg.system_stop(force=True)
        self.stop_agents()

        # Update the provider and write a new server YAML file.
        self._update_server_yaml(new_provider)

        # Restart the DAOS servers and agents with the new config.
        self._restart_servers_and_agents(new_provider)

        # Look for the new provider in the agent dump_attachinfo output
        self.log_step(f"Verify servers and clients are using the new provider {new_provider}")

        clients_using_provider, engines_using_provider = self._parse_attachinfo(new_provider)

        if clients_using_provider != len(self.hostlist_clients):
            self.fail("Some DAOS clients not using provider %s; want %d found %d",
                      new_provider, len(self.hostlist_clients), clients_using_provider)
        if engines_using_provider != self.server_managers[0].engines:
            self.fail("Some DAOS engines not using provider %s; want %d found %d",
                      new_provider, self.server_managers[0].engines, engines_using_provider)

        self.log.info("All DAOS engines and clients using provider %s", new_provider)

        # Check provider in DAOS control log
        line_count = self._scan_control_log_for_uri(new_provider)
        if self.server_managers[0].engines != line_count:
            self.fail(f"Only {line_count} DAOS server using provider {new_provider}")

        self.log.info("Provider change registered in DAOS control log for all engines.")

        # Read file with IOR to verify system works with new provider
        self._ior_read(intercept, new_provider, ior_read_flags)

        # Change the provider back to the original and verify the switch back works
        self.log_step(f"Restoring original provider: {original_provider}")

        # Stop engines and agents again
        self.server_managers[0].dmg.system_stop(force=True)
        self.stop_agents()

        # Update the server YAML file with the original provider
        self._update_server_yaml(original_provider)

        # Restart the DAOS servers and agents with the new config.
        self._restart_servers_and_agents(original_provider)

        # Look for the original provider in the agent dump_attachinfo output
        self.log_step("Verify servers and clients are using the original provider")

        clients_using_provider, engines_using_provider = self._parse_attachinfo(original_provider)

        if clients_using_provider != len(self.hostlist_clients):
            self.fail("Some DAOS clients not using provider %s; want %d found %d",
                      original_provider, len(self.hostlist_clients), clients_using_provider)
        if engines_using_provider != self.server_managers[0].engines:
            self.fail("Some DAOS engines not using provider %s; want %d found %d",
                      original_provider, self.server_managers[0].engines, engines_using_provider)

        self.log.info("All DAOS engines and clients using provider %s", original_provider)

        # Check provider in DAOS control log. There should be 2 * number of engines
        line_count = self._scan_control_log_for_uri(original_provider)
        if line_count != 2 * self.server_managers[0].engines:
            self.fail(f"Some DAOS servers not using provider {original_provider}")

        self.log.info("Provider change registered in DAOS control log for all engines.")

        # Read file with IOR to verify system works with original provider
        self._ior_read(intercept, original_provider, ior_read_flags)
