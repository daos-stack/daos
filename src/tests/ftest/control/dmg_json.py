#!/usr/bin/python

"""
(C) Copyright 2020 Intel Corporation.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
The Government's rights to use, modify, reproduce, release, perform, display,
or disclose this software are subject to the terms of the Apache License as
provided in Contract No. B609815.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""

# import json
import re
from apricot import TestWithServers


class DmgJson(TestWithServers):
    """DmgJson test class.

    Test Class Description:
        Verify output of dmg commands using json format (-j or --json flag)

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgJson test object."""
        super(DmgJson, self).__init__(*args, **kwargs)
        self.dmg = None

    def test_dmg_json(self):
        """Test ID: DAOS-4908.

        Test Description: TODO

        :avocado: tags=all,pr,hw,small,dmg,dmg_json

        """
        # Get parameters from yaml file
        scm_size_expected = self.params.get("scm_size", '/run/expected/')
        ranks_expected = self.params.get("ranks", '/run/expected/')
        #nsvc_expected = self.params.get("nsvc", '/run/expected/')
        self.log.info("\nscm_size_expected:\n%s\n", scm_size_expected)
        #self.log.info("\nnsvc_expected:\n%s\n", nsvc_expected)
        self.log.info("\nranks_expected:\n%s\n", ranks_expected)

        # setup dmg command
        dmg = self.get_dmg_command()
        self.log.info("\ndmg:\n%s\n", dmg)

######################################################
#              "dmg pool" commands                   #
######################################################

    # create
        # Creates a pools to use for subsequent dmg commands to use
        pool_create_data = dmg.pool_create(
            scm_size=scm_size_expected, target_list=ranks_expected)
        self.log.info("\npool_create_json_out:\n%s\n", pool_create_data)

        # if dmg.result.exit_status != 0:
        #     self.fail("    Unable to parse the Pool's UUID and SVC.")

        # get UUID and svc from 'dmg pool create' json output
        uuid = str(pool_create_data["uuid"])
        svc = int(pool_create_data["svc"])
        self.log.info("\npool create uuid: %s, svc: %s\n", uuid, svc)

        # check output
        regex = '[a-f0-9]{8}-?[a-f0-9]{4}-?4[a-f0-9]{3}'\
            '-?[89ab][a-f0-9]{3}-?[a-f0-9]{12}'
        is_valid_uuid = bool(re.match(regex, uuid))
        # is_valid_nsvc = bool(svc == 1)
        self.log.info("\nuuid valid?: %s\n",
                      str(is_valid_uuid))
        # if bool(re.match(regex, uuid)) == False or nsvc_expected != svc:
        #     self.fail("Invalid svc json output!")

    # list
        pool_list_data = dmg.pool_list()
        # self.log.info("\npool list:\n%s\n", pool_list_data)

        # if int(pool_list_data[uuid]) != svc:
        self.log.info("res: %s, res2: %s, res3: %s",
                      pool_list_data[uuid], svc, uuid)
        #     self.fail("Invalid 'svc' json output from 'dmg pool list'")

    # query
        pool_query_data = dmg.pool_query(pool=uuid)
        self.log.info("\npool query:\n%s\n", pool_query_data)

    # :get-acl
        pool_get_acl_data = dmg.pool_get_acl(pool=uuid)
        self.log.info("\npool get-acl:\n%s\n", pool_get_acl_data)

# #####################################################
# #              "dmg system" commands                #
# #####################################################

    # list-pools
        system_list_pools_data = dmg.system_list_pools()
        self.log.info("\nsystem list-pools:\n%s\n", system_list_pools_data)

    # query
        system_query_data = dmg.system_query(verbose=True)
        self.log.info("\nsystem query:\n%s\n", system_query_data)

    # leader-query
        system_leader_query_data = dmg.system_leader_query()
        self.log.info("\nsystem leader-query:\n%s\n", system_leader_query_data)


# ####################################################
# #             "dmg storage" commands               #
# ####################################################

    # query
        # list-devices
        storage_query_list_devices_data = dmg.storage_query_list_devices()
        self.log.info("\nstorage query list-devices:\n%s\n",
                      storage_query_list_devices_data)

        # list-pools
        storage_query_list_pools_data = dmg.storage_query_list_pools(
            verbose=True)
        self.log.info("\nstorage query list-pools:\n%s\n",
                      storage_query_list_pools_data)

        # device-health
        storage_query_device_health_data = dmg.storage_query_device_health()
        self.log.info("\nstorage query device health:\n%s",
                      storage_query_device_health_data)

        # target-health
        storage_query_target_health_data = dmg.storage_query_target_health()
        self.log.info("\nstorage query target health:\n%s",
                      storage_query_target_health_data)
