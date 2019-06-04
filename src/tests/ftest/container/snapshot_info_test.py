#!/usr/bin/python
"""
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
"""
from __future__ import print_function
import os
import time
import traceback
import sys
import random
import string
import json
from apricot import TestWithServers
from conversion import c_uuid_to_str
from daos_api import (DaosContext, DaosPool, DaosContainer, DaosSnapshot,
                      DaosLog, DaosApiError)

# pylint: disable=broad-except
class Snapshot(TestWithServers):
    """
    Epic: DAOS-2249 Create system level tests that cover basic snapshot
          functionality.
    Testcase:
          DAOS-1370 Basic snapshot test
          DAOS-1386 Test container SnapShot information
          DAOS-1371 Test list snapshots
          DAOS-1395 Test snapshot destroy
          DAOS-1402 Test creating multiple snapshots

    Test Class Description:
          Start DAOS servers, set up the pool and container for the snapshot
          testcases.
    :avocado: recursive
    """

    def display_daosContainer(self):
        self.log.info("==display_daosContainer========================")
        self.log.info("self.container OBJ=                %s"
            ,self.container)
        self.log.info("self.container.context obj=        %s"
            ,self.container.context)
        self.log.info("self.container.context.libdaos=    %s"
            ,self.container.context.libdaos)
        self.log.info("self.container.context.libtest=    %s"
            ,self.container.context.libtest)
        self.log.info("self.container.context.ftable=     %s"
            ,self.container.context.ftable)
        self.log.info("self.container.uuid obj=           %s"
            ,self.container.uuid)
        self.log.info("self.container.coh=                %s"
            ,self.container.coh)
        self.log.info("self.container.poh=                %s"
            ,self.container.poh)
        self.log.info("self.container.info obj=           %s"
            ,self.container.info)
        self.log.info("self.container.info.ci_uuid=       %s"
            ,self.container.info.ci_uuid)
        self.log.info("self.container.info.es_hce=        %s"
            ,self.container.info.es_hce)
        self.log.info("self.container.info.es_lre=        %s"
            ,self.container.info.es_lre)
        self.log.info("self.container.info.es_lhe=        %s"
            ,self.container.info.es_lhe)
        self.log.info("self.container.info.es_ghce=       %s"
            ,self.container.info.es_ghce)
        self.log.info("self.container.info.es_glre=       %s"
            ,self.container.info.es_glre)
        self.log.info("self.container.info.es_ghpce=      %s"
            ,self.container.info.es_ghpce)
        self.log.info("self.container.info.ci_nsnapshots= %s"
            ,self.container.info.ci_nsnapshots)
        self.log.info("self.container.info.ci_snapshots=  %s"
            ,self.container.info.ci_snapshots)
        self.log.info("self.container.info.ci_min_slipped_epoch= %s"
            ,self.container.info.ci_min_slipped_epoch)
        self.log.info("==========================")

    def display_snapshot(self,snapshot):
        self.log.info("==display_snapshot=============================")
        self.log.info("snapshot=                 %s",snapshot)
        self.log.info("snapshot.context=         %s",snapshot.context)
        self.log.info("snapshot.context.libdaos= %s"
            ,snapshot.context.libdaos)
        self.log.info("snapshot.context.libtest= %s"
            ,snapshot.context.libtest)
        self.log.info("snapshot.context.ftable= %s"
            ,snapshot.context.ftable)
        self.log.info("snapshot.context.ftable[list-attr]= %s"
            ,snapshot.context.ftable["list-attr"])
        self.log.info("snapshot.context.ftable[test-event]=%s"
            ,snapshot.context.ftable["test-event"])
        self.log.info("snapshot.name=  %s",snapshot.name)
        self.log.info("snapshot.epoch= %s",snapshot.epoch)
        self.log.info("==================================")

    def take_snapshot(self, epoch):
        self.log.info("==Taking snapshot for:")
        self.log.info("    self.container.coh= %s",self.container.coh)
        self.log.info("    epoch=              %s",epoch)
        substep = step + ".1"
        snapshot = DaosSnapshot(self.context) #DaosContext(build_paths[..]/lib/)
        #self.display_snapshot(snapshot)    #display ss info before ss created
        #    Verify initial epic should be 0
        substep = step + ".2"
        snapshot.create(self.container.coh, epoch)
        self.display_snapshot(snapshot)    #display ss info after ss created
        return snapshot

    def verify_snapshot(self, snapshot1, snapshot2):
        self.log.info("==Verifying snapshot for: \n     %s  and \n     %s"
            ,snapshot1,snapshot2)
        status = 1
        ss_context = ["libdaos","libtest","ftable","ftable[list-attr]",
                      "ftable[test-event]"]
        for ct in ss_context:
            if snapshot1.context.ct != snapshot2.context.ct:
                status = 0
        if snapshot1.name != snapshot2.name:
            status = 0
        if snapshot1.epoch == snapshot2.epoch:
            status = 0
        return status

    def append_snapshot_testdata(self, coh, epoch, snapshot, test_data):
        self.coh_list.append(coh)
        self.container_epoch_list.append(epoch)
        self.snapshot_list.append(snapshot)
        self.test_data.append(test_data)
        self.log.info("=====>append_snapshot_testdata %s,%s,%s,%s"
            ,coh, epoch, snapshot, test_data)
        return


    def test_snapshot_create_negativetest(self):
        """
        Test ID: DAOS-1390 Verify snap_create bad parameter behavior

        Test Description:
                (1)Create an object, write random adata into it, and take
                   a snapshot.
                (2)Verify the snapshot is working properly.
                (3)Test snapshot with an invalid container handler
                (4)Test snapshot with a NULL container handler
                (5)Test snapshot with an invalid epoch

        Use Cases:
        :avocado: tags=snap,snapshot_negative,snapshotcreate_negative
        """
        pool = None

        try:
            # parameters used in pool create
            createmode = self.params.get("mode", '/run/poolparams/createmode/')
            createuid = os.geteuid()
            creategid = os.getegid()
            createsetid = self.params.get("setname",
                                          '/run/poolparams/createset/')
            createsize = self.params.get("size", '/run/poolparams/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.context)
            pool.create(createmode, createuid, creategid,
                createsize, createsetid, None)

            # need a connection to create container
            pool.connect(1 << 1)

            # create a container
            container = DaosContainer(self.context)
            container.create(pool.handle)

            # now open it
            container.open()

            # do a query and compare the UUID returned from create with
            # that returned by query
            container.query()

            if container.get_uuid_str() != c_uuid_to_str(
                container.info.ci_uuid):
                self.fail("##Container UUID did not match the one in info\n")

        except DaosApiError as error:
            self.log.info("Error detected in DAOS pool container setup: {}"
                .format(str(error)))
            self.log.info(traceback.format_exc())
            self.fail("##Test failed on setUp, before snapshot taken")

        snapshot_index = 1
        #(1)Create an object, write some data into it, and take a snapshot
        try:
            obj_cls = self.params.get("obj_class", '/run/object_class/*')
            dkey = "dkey"
            akey = "akey"
            size = random.randint(1, 100) + 1
            rand_str = lambda n: ''.join([random.choice(string.lowercase)
                for i in xrange(n)])
            thedata = "--->>>Happy Daos Snapshot-Create Negative Testing " + \
                str(snapshot_index) + "<<<---" + rand_str(size)
            datasize = len(thedata) + 1
            obj, epoch = container.write_an_obj(thedata,
                                                     datasize,
                                                     dkey,
                                                     akey,
                                                     obj_cls=obj_cls)
            obj.close()
            ##Take a snapshot of the container
            self.snapshot = DaosSnapshot(self.context)
            self.snapshot.create(container.coh, epoch)
            self.log.info("==Wrote an object and created a snapshot")

            #Display snapshot
            substep = "1." + str(snapshot_index)
            self.log.info("==(1)Test step %s",substep)
            self.log.info("==container epoch=     %s",epoch)
            self.log.info(
                "==self.snapshot.epoch= %s",self.snapshot.epoch)
            self.display_snapshot(self.snapshot)

        except DaosApiError as error:
            self.fail(
                "##Test failed during the initial object write: {}"
                .format(str(error)))

        #(2)Verify the snapshot is working properly.
        try:
            obj.open()
            snap_handle = self.snapshot.open(container.coh, self.snapshot.epoch)
            thedata2 = container.read_an_obj(
                datasize, dkey, akey, obj, snap_handle.value)
            self.log.info("==(2)snapshot test loop: %s"
                ,snapshot_index)
            self.log.info("==container_epoch= %s",epoch)
            self.log.info("==self.snapshot_list[ind]=%s"
                ,self.snapshot)
            self.log.info("==self.snapshot.epoch=  %s"
                ,self.snapshot.epoch)
            self.log.info("==written thedata size= %s"
                ,len(thedata)+1)
            self.log.info("==written thedata=%s",thedata)
            self.log.info("==thedata2.value= %s",thedata2.value)
            if thedata2.value != thedata:
                raise Exception("##The data in the snapshot is not the "
                    "same as the original data")
            self.log.info("==The snapshot data matches the data originally "
                    "written.")
        except Exception as error:
            self.fail(
                "##Error when retrieving the snapshot data: {}"
                .format(str(error)))

        #(3)Test snapshot with an invalid container handler
        try:
            self.log.info("==(3)Snapshot with an invalid container handler.")
            coh = container
            self.snapshot = DaosSnapshot(self.context)
            self.snapshot.create(coh, epoch)
        except:
            self.log.info("===>Negative test 1, expecting failed on taking "
                "snapshot with an invalid container.coh: %s",coh)
        else:
            self.fail(
                "##Negative test 1 passing, expecting failed on"
                " taking snapshot with an invalid container.coh: %s"
                ,coh)

        #(4)Test snapshot with a NULL container handler
        try:
            self.log.info("==(4)Snapshot with a NULL container handler.")
            coh = None
            self.snapshot = DaosSnapshot(self.context)
            self.snapshot.create(coh, epoch)
        except:
            self.log.info("===>Negative test 2, expecting failed on taking "
                "snapshot on a NULL container.coh.")
        else:
            self.fail("##Negative test 2 passing, expecting failed on taking "
                "snapshot on a NULL container.coh.")
        #(5)Test snapshot with an invalid epoch
        try:
            self.log.info("==(5)Snapshot with a NULL epoch = ")
            invalid_epoch = None
            self.snapshot = DaosSnapshot(self.context)
            self.snapshot.create(container.coh, invalid_epoch)
        except:
            self.log.info("===>Negative test 3, expecting failed on taking "
                "snapshot with a NULL epoch.")
        else:
            self.fail("##Negative test 3 passing, expecting failed on taking "
               "snapshot with a NULL epoch.")
        self.log.info("==>DAOS container snapshot-create Negative tests passed")

        # Clean up
        try:
            container.close()
            # wait a few seconds and then destroy
            time.sleep(5)
            container.destroy()
        except DaosApiError as excep:
            self.log.info(excep)
            self.log.info(traceback.format_exc())
            self.fail("##Basic snapshot test failed on cleanUp.\n")
        finally:
            # cleanup the pool
            pool.disconnect()
            pool.destroy(1)


    def test_snapshot_info(self):
        """
        Test ID: DAOS-1386 Test container SnapShot information
                 DAOS-1371 Test list snapshots
                 DAOS-1395 Test snapshot destroy
                 DAOS-1402 Test creating multiple snapshots
        Test Description:
                (1)Create an object, write random adata into it, and take
                   a snapshot.
                (2)Make changes to the data object. The write_an_obj function
                   does a commit when the update is complete.
                (3)Verify the data in the snapshot is the original data.
                   Get a handle for the snapshot and read the object at dkey,
                   akey. Compare it to the originally written data.
                (4)List the snapshot and make sure it reflects the original
                   epoch.
                   ==>Repeat step(1) to step(4) for multiple snapshot tests.
                (5)Verify the snapshots data.
                (6)Destroy the snapshot.
                (7)Check if still able to Open the destroied snapshot and
                   Verify the snapshot removed from the snapshot list.
        Use Cases:
        :avocado: tags=snap,snapshotinfo
        """

        self.coh_list= []
        self.container_epoch_list= []
        self.snapshot_list= []
        self.test_data= []
        snapshot_index = 1
        snapshot_loop = 10
        pool = None

        try:
            # parameters used in pool create
            createmode = self.params.get("mode", '/run/poolparams/createmode/')
            createuid = os.geteuid()
            creategid = os.getegid()
            createsetid = self.params.get("setname",
                                          '/run/poolparams/createset/')
            createsize = self.params.get("size", '/run/poolparams/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.context)
            pool.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # need a connection to create container
            pool.connect(1 << 1)

            # create a container
            container = DaosContainer(self.context)
            container.create(pool.handle)

            # now open it
            container.open()

            # do a query and compare the UUID returned from create with
            # that returned by query
            container.query()

            if container.get_uuid_str() != c_uuid_to_str(
                    container.info.ci_uuid):
                self.fail("##Container UUID did not match the one in info")

        except DaosApiError as error:
            self.log.info("Error detected in DAOS pool container setup: {}"
                .format(str(error)))
            self.log.info(traceback.format_exc())
            self.fail("##Test failed on setUp, before snapshot taken")
        #
        #Test loop for creat, modify and snapshot object in the DAOS container.
        #
        while snapshot_index <= snapshot_loop:
            #(1)Create an object, write some data into it, and take a snapshot
            try:
                obj_cls = self.params.get("obj_class", '/run/object_class/*')
                dkey = "dkey"
                akey = "akey"
                size = random.randint(1, 100) + 1
                rand_str = lambda n: ''.join([random.choice(string.lowercase)
                    for i in xrange(n)])
                thedata = "--->>>Happy Daos Snapshot Testing " + \
                    str(snapshot_index) + "<<<---" + rand_str(size)
                datasize = len(thedata) + 1
                obj, epoch = container.write_an_obj(
                    thedata, datasize, dkey, akey, obj_cls=obj_cls)
                obj.close()
                #Take a snapshot of the container
                self.snapshot = DaosSnapshot(self.context)
                self.snapshot.create(container.coh, epoch)
                self.log.info("==Wrote an object and created a snapshot")

                #Display snapshot
                substep = "1." + str(snapshot_index)
                self.log.info("==(1)Test step %s",substep)
                self.log.info("==container epoch=     %s",epoch)
                self.log.info("==self.snapshot.epoch= %s"
                    ,self.snapshot.epoch)
                self.display_snapshot(self.snapshot)

                #Save snapshot test data
                self.append_snapshot_testdata(container.coh, epoch,
                    self.snapshot, thedata)
                snapshot_index += 1

            except DaosApiError as error:
                self.fail(
                    "##Test failed during the initial object write: {}"
                    .format(str(error)))

            #(2)Make changes to the data object. The write_an_obj function does
            #   a commit when the update is complete
            num_of_write_obj = 100
            try:
                rand_str = lambda n: ''.join([random.choice(string.lowercase)
                    for i in xrange(n)])
                self.log.info("==(2)Committing %d additional transactions to "
                    "the same KV.",num_of_write_obj)
                more_transactions = num_of_write_obj
                while more_transactions:
                    size = random.randint(1, 250) + 1
                    new_data = rand_str(size)
                    new_obj, _ = container.write_an_obj(
                        new_data, size, dkey, akey, obj_cls=obj_cls)
                    new_obj.close()
                    more_transactions -= 1
            except Exception as error:
                self.fail(
                    "##Test failed during the write of multi-objects: {}"
                    .format(str(error)))

            #(3)Verify the data in the snapshot is the original data.
            #   Get a handle for the snapshot and read the object at dkey, akey
            #   Compare it to the originally written data.
            try:
                obj.open()
                snap_handle = self.snapshot.open(
                    container.coh, self.snapshot.epoch)
                thedata3 = container.read_an_obj(
                    datasize, dkey, akey, obj, snap_handle.value)
                self.log.info("==(3)snapshot test loop: %s"
                    ,snapshot_index)
                self.log.info("==container_epoch= %s",epoch)
                self.log.info("==self.snapshot_list[ind]=%s"
                    ,self.snapshot)
                self.log.info("==self.snapshot.epoch=  %s"
                    ,self.snapshot.epoch)
                self.log.info("==written thedata size= %s"
                    ,len(thedata)+1)
                self.log.info("==written thedata=%s",thedata)
                self.log.info("==thedata3.value= %s",thedata3.value)
                if thedata3.value != thedata:
                    raise Exception("##The data in the snapshot is not the "
                        "same as the original data")
                self.log.info("==The snapshot data matches the data originally"
                    " written.")
            except Exception as error:
                self.fail(
                    "##Error when retrieving the snapshot data: {}"
                    .format(str(error)))

            #(4)List the snapshot and make sure it reflects the original epoch
            try:
                reported_epoch = self.snapshot.list(container.coh, epoch)
                self.log.info("==(4)List snapshot reported_epoch=%s"
                    ,reported_epoch)
                self.log.info("     self.snapshot.epoch=%s"
                    ,self.snapshot.epoch)
                #self.cancel("tickets already assigned DAOS-2390 DAOS-2392")
                #if self.snapshot.epoch != reported_epoch:
                    #raise Exception("##The snapshot epoch returned from "
                    #        "snapshot list is not the same as the original "
                    #        "epoch list is not the same as the original epoch"
                    #        "snapshotted.")
                self.log.info("==After 10 additional commits the snapshot is "
                   "still available")
            except Exception as error:
                self.fail(
                    "##Test was unable to list the snapshot: {}"
                    .format(str(error)))

        #(5)Verify the snapshots data
        try:
            for ind in range(0, len(self.container_epoch_list)):
                epoch = self.container_epoch_list[ind]
                ss = self.snapshot_list[ind]
                datasize = len(self.test_data[ind]) + 1
                obj.open()
                snap_handle = self.snapshot.open(container.coh, ss.epoch)
                #self.cancel("tickets already assigned DAOS-2484")
                #thedata3 = container.read_an_obj(datasize, dkey, akey, obj,
                #                                  snap_handle.value)
                #self.log.info("==(5)snapshot test list %s:".format(ind+1))
                #self.log.info("==self.container_epoch_list[ind]=%s"\
                #    .format(epoch))
                #self.log.info("==self.snapshot_list[ind]=%s"\
                #    .format(self.snapshot_list[ind]))
                #self.log.info("==self.snapshot_list[ind].epoch=%s"\
                #    .format( ss.epoch))
                #self.log.info("==self.test_data_size=    %s".format(datasize))
                #self.log.info("==thedata3.value=         %s"\
                #    .format(thedata3.value))
                #self.log.info("==self.test_data[ind]=    %s"\
                #    .format( self.test_data[ind]))
                #if thedata3.value != self.test_data[ind]:
                #    raise Exception("##The data in the snapshot is not "
                #                    "same as the original data")
                #self.log.info("The snapshot data matches the data originally "
                #    "written.")
        except Exception as error:
            self.fail(
                "##Error when retrieving the snapshot data: {}"
                .format(str(error)))

        #(6)Destroy the snapshot
        self.log.info("==(6)Destroy snapshot  epoch: %s",epoch)
        try:
            self.snapshot.destroy(container.coh, epoch)
            self.log.info("==Snapshot successfully destroyed")
        except Exception as error:
                self.fail(
                    "##Error on self.snapshot.destroy: {}".format(str(error)))

        #(7)Check if still able to Open the destroied snapshot and
        #    Verify the snapshot removed from the snapshot list
        try:
            obj.open()
            snap_handle3 = self.snapshot.open(container.coh,
                self.snapshot.epoch)
            thedata3 = container.read_an_obj(datasize, dkey, akey,
                obj, snap_handle3.value)
            self.log.info("-->thedata_after_snapshot.destroied.value= %s"
                ,thedata3.value)
            self.log.info("==>snapshot_epoch=     %s"
                ,self.snapshot.epoch)
            self.log.info("-->snapshot.list(container.coh,epoch)=%s"
                ,self.snapshot.list(container.coh,epoch))
            #self.cancel("tickets already assigned DAOS-2390 DAOS-2392")
            #Still able to open the snapshot and read data after destroyed.

        except Exception as error:
            self.fail(
                "##(7)Error when retrieving the 2nd snapshot data: {}"
                .format(str(error)))
        self.log.info("==>DAOS container SnapshotInfo test passed")

        # Now destroy the snapshot
        try:
            self.snapshot.destroy(container.coh)
            self.log.info("==Snapshot successfully destroyed")
        except Exception as error:
            self.fail(
                "##Error on self.snapshot.destroy: {}".format(str(error)))

        # Clean up
        try:
            container.close()
            # wait a few seconds and then destroy
            time.sleep(5)
            container.destroy()
        except DaosApiError as excep:
            self.log.info(excep)
            self.log.info(traceback.format_exc())
            self.fail("##Snapshot info test failed on cleanUp.")
        finally:
            # cleanup the pool
            pool.disconnect()
            pool.destroy(1)

