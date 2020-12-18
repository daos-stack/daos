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
from nvme_utils import ServerFillUp

class ErasureCodeIor(ServerFillUp):
    # pylint: disable=too-many-ancestors
    """
    Class to used for EC testing.
    It will get the object types from yaml file write the IOR data set with
    IOR.
    """
    def __init__(self, *args, **kwargs):
        """Initialize a ServerFillUp object."""
        super(ErasureCodeIor, self).__init__(*args, **kwargs)
        self.server_count = None
        self.cont_uuid = []

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(ErasureCodeIor, self).setUp()

        self.obj_class = self.params.get("dfs_oclass", '/run/ior/objectclass/*')
        self.ior_chu_trs_blk_size = self.params.get(
            "chunk_block_transfer_sizes", '/run/ior/*')
        #Fail IOR test in case of Warnings
        self.fail_on_warning = True
        self.server_count = len(self.hostlist_servers) * 2
        #Create the Pool
        self.create_pool_max_size()

    def ior_param_update(self, oclass, sizes):
        """Update the IOR command parameters.

        Args:
            oclass(list): list of the obj class to use with IOR
            sizes(list): Update Transfer, Chunk and Block sizes
        """
        self.ior_cmd.dfs_oclass.update(oclass[0])
        self.ior_cmd.dfs_dir_oclass.update(oclass[0])
        self.ior_cmd.dfs_chunk.update(sizes[0])
        self.ior_cmd.block_size.update(sizes[1])
        self.ior_cmd.transfer_size.update(sizes[2])

    def ior_write_dataset(self):
        """ Write IOR data set with different EC object and different sizes
        """
        for oclass in self.obj_class:
            for sizes in self.ior_chu_trs_blk_size:
                #Skip the object type if server count does not meet the minimum
                #EC object server count
                if oclass[1] > self.server_count:
                    continue
                self.ior_param_update(oclass, sizes)
                self.update_ior_cmd_with_pool(oclass=oclass[0])
                #Start IOR Write
                self.start_ior_load(operation="WriteRead", percent=1,
                                    create_cont=False)
                self.cont_uuid.append(self.ior_cmd.dfs_cont.value)

    def ior_read_dataset(self, parity=1):
        """Read IOR data and verify for different EC object and different sizes.

        Args:
           data_parity(str): object parity type for reading, default All.
        """
        con_count = 0
        for oclass in self.obj_class:
            for sizes in self.ior_chu_trs_blk_size:
                #Skip the object type if server count does not meet the minimum
                #EC object server count
                if oclass[1] > self.server_count:
                    continue
                parity_set = "P{}".format(parity)
                #Read the requested data+parity data set only
                if parity != 1 and parity_set not in oclass[0]:
                    print("Skipping Read as object type is {}"
                          .format(oclass[0]))
                    con_count += 1
                    continue
                self.ior_param_update(oclass, sizes)
                self.container.uuid = self.cont_uuid[con_count]
                #Start IOR Read
                self.start_ior_load(operation='Read', percent=1,
                                    create_cont=False)
                con_count += 1
