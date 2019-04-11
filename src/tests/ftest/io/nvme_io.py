#!/usr/bin/python
'''
  (C) Copyright 2019 Intel Corporation.

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
'''
from __future__ import print_function

import os
import sys
import json
import traceback
import uuid
import re
import avocado

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import server_utils
import write_host_file
import ior_utils

from general_utils import DaosTestError
from daos_api import DaosContext, DaosPool, DaosApiError, DaosLog

class NvmeIo(avocado.Test):
    """
    Test Class Description:
        Test the general Metadata operations and boundary conditions.
    """

    def setUp(self):
        self.pool = None
        self.hostlist = None
        self.hostfile_clients = None
        self.hostfile = None
        self.out_queue = None
        self.pool_connect = False

        with open('../../../.build_vars.json') as json_f:
            build_paths = json.load(json_f)

        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        self.server_group = self.params.get("name", '/server_config/',
                                            'daos_server')
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.d_log = DaosLog(self.context)
        self.hostlist = self.params.get("servers", '/run/hosts/*')
        self.hostfile = write_host_file.write_host_file(self.hostlist,
                                                        self.workdir)
        #Start Server
        server_utils.run_server(self.hostfile, self.server_group, self.basepath)

    def tearDown(self):
        try:
            if self.pool_connect:
                self.pool.disconnect()
                self.pool.destroy(1)
        finally:
            server_utils.stop_server(hosts=self.hostlist)

    def verify_pool_size(self, original_pool_info, ior_args):
        """
        Function is to validate the pool size
        original_pool_info: Pool info prior to IOR
        ior_args: IOR args to calculate the file size
        """
        #Get the current pool size for comparison
        current_pool_info = self.pool.pool_query()
        #if Transfer size is < 4K, Pool size will verified against NVMe, else
        #it will be checked against SCM
        if ior_args['stripe_size'] >= 4096:
            print("Size is > 4K,Size verification will be done with NVMe size")
            storage_index = 1
        else:
            print("Size is < 4K,Size verification will be done with SCM size")
            storage_index = 0

        free_pool_size = (
            original_pool_info.pi_space.ps_space.s_free[storage_index]
            - current_pool_info.pi_space.ps_space.s_free[storage_index])

        obj_multiplier = 1
        replica_number = re.findall(r'\d+', "ior_args['object_class']")
        if replica_number:
            obj_multiplier = int(replica_number[0])
        expected_pool_size = (ior_args['slots'] * ior_args['block_size'] *
                              obj_multiplier)

        if free_pool_size < expected_pool_size:
            raise DaosTestError(
                'Pool Free Size did not match Actual = {} Expected = {}'
                .format(free_pool_size, expected_pool_size))

    @avocado.fail_on(DaosApiError)
    def test_nvme_io(self):
        """
        Test ID: DAOS-2082
        Test Description: Test will run IOR with standard and non standard
        sizes.IOR will be run for all Object type supported. Purpose is to
        verify pool size (SCM and NVMe) for IOR file.
        This test is running multiple IOR on same server start instance.
        :avocado: tags=nvme,nvme_io,large
        """
        ior_args = {}

        hostlist_clients = self.params.get("clients", '/run/hosts/*')
        tests = self.params.get("ior_sequence", '/run/ior/*')
        object_type = self.params.get("object_type", '/run/ior/*')
        #Loop for every IOR object type
        for obj_type in object_type:
            for ior_param in tests:
                self.hostfile_clients = write_host_file.write_host_file(
                    hostlist_clients,
                    self.workdir,
                    ior_param[4])
                #There is an issue with NVMe if Transfer size>64M, Skipped this
                #sizes for now
                if ior_param[2] > 67108864:
                    print ("Xfersize > 64M getting failed, DAOS-1264")
                    continue

                self.pool = DaosPool(self.context)
                self.pool.create(self.params.get("mode",
                                                 '/run/pool/createmode/*'),
                                 os.geteuid(),
                                 os.getegid(),
                                 ior_param[0],
                                 self.params.get("setname",
                                                 '/run/pool/createset/*'),
                                 nvme_size=ior_param[1])
                self.pool.connect(1 << 1)
                self.pool_connect = True
                createsvc = self.params.get("svcn", '/run/pool/createsvc/')
                svc_list = ""
                for i in range(createsvc):
                    svc_list += str(int(self.pool.svc.rl_ranks[i])) + ":"
                svc_list = svc_list[:-1]

                ior_args['client_hostfile'] = self.hostfile_clients
                ior_args['pool_uuid'] = self.pool.get_uuid_str()
                ior_args['svc_list'] = svc_list
                ior_args['basepath'] = self.basepath
                ior_args['server_group'] = self.server_group
                ior_args['tmp_dir'] = self.workdir
                ior_args['iorflags'] = self.params.get("iorflags",
                                                       '/run/ior/*')
                ior_args['iteration'] = self.params.get("iteration",
                                                        '/run/ior/*')
                ior_args['stripe_size'] = ior_param[2]
                ior_args['block_size'] = ior_param[3]
                ior_args['stripe_count'] = self.params.get("stripecount",
                                                           '/run/ior/*')
                ior_args['async_io'] = self.params.get("asyncio",
                                                       '/run/ior/*')
                ior_args['object_class'] = obj_type
                ior_args['slots'] = ior_param[4]

                #IOR is going to use the same --daos.stripeSize,
                #--daos.recordSize and Transfer size.
                try:
                    size_before_ior = self.pool.pool_query()
                    ior_utils.run_ior(ior_args['client_hostfile'],
                                      ior_args['iorflags'],
                                      ior_args['iteration'],
                                      ior_args['block_size'],
                                      ior_args['stripe_size'],
                                      ior_args['pool_uuid'],
                                      ior_args['svc_list'],
                                      ior_args['stripe_size'],
                                      ior_args['stripe_size'],
                                      ior_args['stripe_count'],
                                      ior_args['async_io'],
                                      ior_args['object_class'],
                                      ior_args['basepath'],
                                      ior_args['slots'],
                                      filename=str(uuid.uuid4()),
                                      display_output=True)
                    self.verify_pool_size(size_before_ior, ior_args)
                except ior_utils.IorFailed as exe:
                    print (exe)
                    print (traceback.format_exc())
                    self.fail()
                try:
                    if self.pool_connect:
                        self.pool.disconnect()
                        self.pool_connect = False
                    if self.pool:
                        self.pool.destroy(1)
                except DaosApiError as exe:
                    print (exe)
                    self.fail("Failed to Destroy/Disconnect the Pool")
