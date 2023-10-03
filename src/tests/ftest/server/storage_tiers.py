"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import yaml

from apricot import TestWithServers
from command_utils_base import CommonConfig
from server_utils import DaosServerTransportCredentials, DaosServerYamlParameters


class StorageTiers(TestWithServers):
    """Daos server storage configuration tests.

    Test Class Description:
        Simple test to verify storage tiers are correctly obtained from the test yaml file.

    :avocado: recursive
    """

    def test_tiers(self):
        """JIRA ID: DAOS-1525.

        Test Description:
            Verify storage tiers are correctly obtained from the test yaml file.

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=server,storage,storage_tiers
        :avocado: tags=StorageTiers,test_tiers
        """
        expected = []
        for engine in range(2):
            storage = []
            for tier in range(5):
                namespace = os.path.join(
                    self.server_config_namespace[:-1], "engines", str(engine), "storage", str(tier),
                    "*")
                storage_class = self.params.get("class", namespace, None)
                if not storage_class:
                    break
                if storage_class in ["dcpm"]:
                    data = {
                        "class": storage_class,
                        "scm_mount": self.params.get("scm_mount", namespace),
                        "scm_list": self.params.get("scm_list", namespace)
                    }
                elif storage_class in ["ram"]:
                    data = {
                        "class": storage_class,
                        "scm_mount": self.params.get("scm_mount", namespace),
                        "scm_size": self.params.get("scm_size", namespace)
                    }
                else:
                    data = {
                        "class": storage_class,
                        "bdev_list": self.params.get("bdev_list", namespace),
                    }
                    storage_roles = self.params.get("roles", namespace, None)
                    if storage_roles:
                        data["roles"] = storage_roles
                storage.append(data)
            expected.append(storage)
        self.log.info("expected:\n%s", yaml.dump(expected, default_flow_style=False))

        common_config = CommonConfig("daos_server", DaosServerTransportCredentials())
        config = DaosServerYamlParameters(None, common_config)
        config.namespace = self.server_config_namespace
        config.get_params(self)
        data = config.get_yaml_data()
        self.log.info("server config:\n%s", yaml.dump(data, default_flow_style=False))

        failed = False
        self.log.info("Verifying storage configuration")
        for engine in range(2):
            for tier in range(len(expected[engine])):
                if data["engines"][engine]["storage"][tier] != expected[engine][tier]:
                    self.log.info("  Verifying engine %s, storage tier %s .. failed", engine, tier)
                    self.log.error("    engine %s, storage tier %s does not match:", engine, tier)
                    self.log.error("      expected: %s", expected[engine][tier])
                    self.log.error("      actual:   %s", data["engines"][engine]["storage"][tier])
                    failed = True
                else:
                    self.log.info("  Verifying engine %s, storage tier %s .. passed", engine, tier)
        if failed:
            self.fail("Error verifying storage tiers")
        self.log.info("Test passed")
