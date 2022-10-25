"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import yaml

from apricot import TestWithServers
from command_utils_base import CommonConfig
from server_utils import DaosServerTransportCredentials, DaosServerYamlParameters
from server_utils_params import MAX_STORAGE_TIERS


class StorageTiers(TestWithServers):
    """Daos server storage configuration tests.

    Test Class Description:
        Simple test to verify storage tiers are correctly obtained from the test yaml file.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DaosServerConfigTest object."""
        super().__init__(*args, **kwargs)
        self.start_agents_once = False
        self.start_servers_once = False
        self.setup_start_agents = False
        self.setup_start_servers = False

    def test_tiers(self):
        """JIRA ID: DAOS-1525.

        Test Description:
            Verify storage tiers are correctly obtained from the test yaml file.

        :avocado: tags=all,pr
        :avocado: tags=hw,small
        :avocado: tags=server,storage,storage_tiers
        :avocado: tags=test_tiers
        """
        expected = [[], []]
        for engine in range(2):
            for tier in range(MAX_STORAGE_TIERS):
                namespace = f"/run/server_config/engines/{engine}/storage/{tier}/*"
                if tier == 0:
                    expected[engine].append({
                        "class": self.params.get("class", namespace),
                        "scm_list": self.params.get("scm_list", namespace),
                        "scm_mount": self.params.get("scm_mount", namespace)
                    })
                elif tier == 1:
                    expected[engine].append({
                        "class": self.params.get("class", namespace),
                        "bdev_list": self.params.get("bdev_list", namespace),
                    })
                else:
                    expected[engine].append({
                        "class": self.params.get("class", namespace),
                        "bdev_list": self.params.get("bdev_list", namespace),
                        "role": self.params.get("role", namespace)
                    })

        common_config = CommonConfig("daos_server", DaosServerTransportCredentials())
        config = DaosServerYamlParameters(None, common_config)

        errors = []
        for max_tiers in range(MAX_STORAGE_TIERS):
            self.log.info("*" * 80)
            self.log.info("Generating a server config with %s storage tiers", max_tiers)
            config.max_storage_tiers = max_tiers
            config.get_params(self)
            data = config.get_yaml_data()
            self.log.info("server config:\n%s", yaml.dump(data, default_flow_style=False))

            self.log.info("Verifying storage configuration")
            for engine in range(2):
                description = f"{max_tiers} storage tiers: engine {engine} configuration"
                if max_tiers == 0:
                    if "storage" in data["engines"][engine]:
                        errors.append(f"{description} contains a 'storage' entry")
                        self.log.error("  %s", errors[-1])
                        continue
                else:
                    if data["engines"][engine]["storage"] != expected[engine][0:max_tiers]:
                        errors.append(f"{description}s do not match")
                        self.log.error("  %s", errors[-1])
                        errors.append(f"  actual:   {data['engines'][engine]['storage']}")
                        self.log.error("  %s", errors[-1])
                        errors.append(f"  expected: {expected[engine][0:max_tiers]}")
                        self.log.error("  %s", errors[-1])
                        continue
                self.log.info("  %s passed", description)
        if errors:
            self.log.info("*" * 80)
            self.log.info("Error summary")
            for error in errors:
                self.log.info("  %s", error)
            self.fail("Error verifying storage tiers")
        self.log.info("Test passed")
