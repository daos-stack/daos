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

import time

from ior_test_base import IorTestBase
from dmg_utils import check_system_query_status
from pydaos.raw import DaosSnapshot


class IoAggregation(IorTestBase):
    """Test class Description: DAOS server does not need to be restarted
                               when the application crashes.
    :avocado: recursive
    """

    def setUp(self):
        """Set up test before executing"""
        super(IoAggregation, self).setUp()
        self.dmg = self.get_dmg_command()

    def display_free_space(self):
        """ Display pool free space """
        free_space = self.pool.get_pool_free_space("scm")
        self.log.info("Free nvme space: %s", free_space)

        return free_space

    def test_ioaggregation(self):
        """Jira ID: DAOS-4332.
        Test Description:
            DAOS server does not need to be restarted when the application
            crashes.
        Use Cases:
            Run IOR over dfuse.
            Cancel IOR in the middle of io.
            Check daos server does not need to be restarted when the
            application crashes.
        :avocado: tags=all,daosio,hw,medium,ib2,full_regression,ioaggregation
        """
        # obtain ior write flags
        self.ior_cmd.signatute.update("123")
        # run ior and crash it during write process
        self.run_ior_with_pool()

        free_space_before_snap = self.display_free_space()

        # create snapshot
        self.container.create_snap()
#        self.container.open()
#        snapshot = DaosSnapshot(self.context)
#        snapshot.create(self.container.container.coh)
#        self.log.info("Created Snapshot with Epoch: %s", snapshot.epoch)

        # write to same ior file again
        self.ior_cmd.signatute.update("456")
        self.run_ior_with_pool(create_cont=False)

        free_space_after_another_ior_write = self.display_free_space()
        
        # delete snapshot
        self.container.destroy_snap(epoch=self.container.epoch)

#        snapshot.destroy(self.container.coh, snapshot.epoch)
#        self.log.info("Destroyed Snapshot with Epoch: %s", snapshot.epoch)

        # check for aggregation to begin

        # shutdown the servers and restart
        self.get_dmg_command().system_stop(True)
        time.sleep(5)
        self.get_dmg_command().system_start()

        # check if all servers started as expected
        scan_info = self.get_dmg_command().get_output("system_query")
        if not check_system_query_status(scan_info):
            self.fail("One or more servers crashed")

        # check if aggregation finished
        time.sleep(30)
        free_space_after_aggregation = self.display_free_space()

#        if free_space_before_snap != free_space_after_aggregation:
#            self.fail("Aggregation is not returning the space properly")








        # check if ior write has started
#        self.check_subprocess_status()
        # allow 50 secs of write to happen
#        time.sleep(50)
        # kill ior process in the middle of IO
#        self.stop_ior()

        # obtain server rank info using 'dmg system query -v'
#        scan_info = self.dmg.get_output("system_query")
        # check for any crashed servers after killing ior in the middle
#        if not check_system_query_status(scan_info):
#            self.fail("One or more server crashed")

        # run ior again and crash it during read process
#        self.run_ior_with_pool()
        # allow write to finish which is set at stonewalling limit of 100 sec
        # hence allowing extra 5 secs for read to begin
#        time.sleep(105)
        # check if ior read process started
#        self.check_subprocess_status("read")
        # kill ior process in middle of read process
#       self.stop_ior()

        # obtain server rank info using 'dmg system query -v'
#        scan_info = self.dmg.get_output("system_query")
        # check for any crashed servers after killing ior in the middle
#        if not check_system_query_status(scan_info):
#            self.fail("One or more server crashed")

        # run ior again if everything goes well till now and allow it to
        # complete without killing in the middle this time to check
        # if io goes as expected after crashing it previously
#        self.run_ior_with_pool()
#        self.mpirun.wait()
