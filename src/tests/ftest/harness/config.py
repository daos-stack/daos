"""
(C) Copyright 2021-2024 Intel Corporation.
(C) Copyright 2025 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import yaml
from apricot import TestWithServers
from dmg_utils import DmgCommand
from general_utils import nodeset_append_suffix


class HarnessConfigTest(TestWithServers):
    """Harness config test cases.

    :avocado: recursive
    """

    def test_harness_config(self):
        """Verify the config handling.

        Verifies the following:
            TestWithServers.mgmt_svc_replicas
            DaosAgentYamlParameters.exclude_fabric_ifaces

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness
        :avocado: tags=HarnessConfigTest,test_harness_config
        """
        self.log.info('Verify mgmt_svc_replicas_suffix set from yaml')
        mgmt_svc_replicas_suffix = self.params.get("mgmt_svc_replicas_suffix", "/run/setup/*")
        self.assertEqual(self.mgmt_svc_replicas_suffix, mgmt_svc_replicas_suffix)

        self.log.info('Verify mgmt_svc_replicas_suffix is appended exactly once')
        mgmt_svc_replicas = nodeset_append_suffix(self.mgmt_svc_replicas, mgmt_svc_replicas_suffix)
        self.assertEqual(sorted(self.mgmt_svc_replicas), sorted(mgmt_svc_replicas))
        mgmt_svc_replicas = nodeset_append_suffix(mgmt_svc_replicas, mgmt_svc_replicas_suffix)
        self.assertEqual(sorted(self.mgmt_svc_replicas), sorted(mgmt_svc_replicas))

        self.log.info('Verify self.get_dmg_command().hostlist_suffix and hostlist')
        dmg = self.get_dmg_command()
        self.assertEqual(dmg.hostlist_suffix, self.mgmt_svc_replicas_suffix)
        expected_hostlist = sorted(nodeset_append_suffix(dmg.hostlist,
                                                         self.mgmt_svc_replicas_suffix))
        self.assertEqual(sorted(dmg.hostlist), expected_hostlist)

        self.log.info('Verify self.get_dmg_command().yaml.get_yaml_data()["hostlist"]')
        self.assertEqual(sorted(dmg.yaml.get_yaml_data()['hostlist']), expected_hostlist)

        self.log.info('Verify DmgCommand().hostlist_suffix and hostlist')
        dmg2 = DmgCommand(self.bin, hostlist_suffix=mgmt_svc_replicas_suffix)
        dmg2.hostlist = dmg.hostlist
        self.assertEqual(dmg2.hostlist_suffix, self.mgmt_svc_replicas_suffix)
        self.assertEqual(sorted(dmg2.hostlist), expected_hostlist)

        self.log.info('Verify server_manager...get_yaml_data...mgmt_svc_replicas"]')
        yaml_data = self.server_managers[0].manager.job.yaml.get_yaml_data()
        self.assertEqual(sorted(yaml_data['mgmt_svc_replicas']), sorted(self.mgmt_svc_replicas))

        self.log.info('Verify daos_server.yaml mgmt_svc_replicas')
        with open(self.server_managers[0].manager.job.temporary_file, 'r') as yaml_file:
            daos_server_yaml = yaml.safe_load(yaml_file.read())
        self.assertEqual(sorted(daos_server_yaml['mgmt_svc_replicas']),
                         sorted(self.mgmt_svc_replicas))

        self.log.info('Verify daos_control.yaml hostlist')
        with open(self.get_dmg_command().temporary_file, 'r') as yaml_file:
            daos_agent_yaml = yaml.safe_load(yaml_file.read())
        self.assertEqual(sorted(daos_agent_yaml['hostlist']), expected_hostlist)

        self.log.info('Verify daos_agent.yaml access_points')
        with open(self.agent_managers[0].manager.job.temporary_file, 'r') as yaml_file:
            daos_agent_yaml = yaml.safe_load(yaml_file.read())
        self.assertEqual(sorted(daos_agent_yaml['access_points']), sorted(self.mgmt_svc_replicas))

        self.log.info('Verify daos_agent.yaml exclude_fabric_ifaces')
        expected = self.params.get('exclude_fabric_ifaces', '/run/agent_config/*')
        self.assertNotIn(expected, (None, []), 'exclude_fabric_ifaces not set in yaml')
        with open(self.agent_managers[0].manager.job.temporary_file, 'r') as yaml_file:
            daos_agent_yaml = yaml.safe_load(yaml_file.read())
        self.assertEqual(daos_agent_yaml['exclude_fabric_ifaces'], expected)

        self.log.info('Verify rand_seed set from yaml')
        rand_seed = self.params.get("rand_seed", "/run/setup/*")
        self.assertEqual(self.rand_seed, rand_seed)
