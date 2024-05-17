"""
(C) Copyright 2021-2022 Intel Corporation.

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
            TestWithServers.access_points
            DaosAgentYamlParameters.exclude_fabric_ifaces

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness
        :avocado: tags=HarnessConfigTest,test_harness_config
        """
        self.log.info('Verify access_points_suffix set from yaml')
        access_points_suffix = self.params.get("access_points_suffix", "/run/setup/*")
        self.assertEqual(self.access_points_suffix, access_points_suffix)

        self.log.info('Verify access_points_suffix is appended exactly once')
        access_points = nodeset_append_suffix(self.access_points, access_points_suffix)
        self.assertEqual(sorted(self.access_points), sorted(access_points))
        access_points = nodeset_append_suffix(access_points, access_points_suffix)
        self.assertEqual(sorted(self.access_points), sorted(access_points))

        self.log.info('Verify self.get_dmg_command().hostlist_suffix and hostlist')
        dmg = self.get_dmg_command()
        self.assertEqual(dmg.hostlist_suffix, self.access_points_suffix)
        expected_hostlist = sorted(nodeset_append_suffix(dmg.hostlist, self.access_points_suffix))
        self.assertEqual(sorted(dmg.hostlist), expected_hostlist)

        self.log.info('Verify self.get_dmg_command().yaml.get_yaml_data()["hostlist"]')
        self.assertEqual(sorted(dmg.yaml.get_yaml_data()['hostlist']), expected_hostlist)

        self.log.info('Verify DmgCommand().hostlist_suffix and hostlist')
        dmg2 = DmgCommand(self.bin, hostlist_suffix=access_points_suffix)
        dmg2.hostlist = dmg.hostlist
        self.assertEqual(dmg2.hostlist_suffix, self.access_points_suffix)
        self.assertEqual(sorted(dmg2.hostlist), expected_hostlist)

        self.log.info('Verify server_manager...get_yaml_data...access_points"]')
        yaml_data = self.server_managers[0].manager.job.yaml.get_yaml_data()
        self.assertEqual(sorted(yaml_data['access_points']), sorted(self.access_points))

        self.log.info('Verify daos_server.yaml access_points')
        with open(self.server_managers[0].manager.job.temporary_file, 'r') as yaml_file:
            daos_server_yaml = yaml.safe_load(yaml_file.read())
        self.assertEqual(sorted(daos_server_yaml['access_points']), sorted(self.access_points))

        self.log.info('Verify daos_control.yaml hostlist')
        with open(self.get_dmg_command().temporary_file, 'r') as yaml_file:
            daos_agent_yaml = yaml.safe_load(yaml_file.read())
        self.assertEqual(sorted(daos_agent_yaml['hostlist']), expected_hostlist)

        self.log.info('Verify daos_agent.yaml access_points')
        with open(self.agent_managers[0].manager.job.temporary_file, 'r') as yaml_file:
            daos_agent_yaml = yaml.safe_load(yaml_file.read())
        self.assertEqual(sorted(daos_agent_yaml['access_points']), sorted(self.access_points))

        self.log.info('Verify daos_agent.yaml exclude_fabric_ifaces')
        expected = self.params.get('exclude_fabric_ifaces', '/run/agent_config/*')
        self.assertNotIn(expected, (None, []), 'exclude_fabric_ifaces not set in yaml')
        with open(self.agent_managers[0].manager.job.temporary_file, 'r') as yaml_file:
            daos_agent_yaml = yaml.safe_load(yaml_file.read())
        self.assertEqual(daos_agent_yaml['exclude_fabric_ifaces'], expected)
