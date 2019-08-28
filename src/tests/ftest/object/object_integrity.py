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

import os
import sys
import json
import ctypes
import time
import avocado
import random
import agent_utils
import server_utils
import write_host_file

from daos_api import (DaosContext, DaosPool, DaosContainer, IORequest, DaosObj,
                      DaosApiError, DaosLog)

class ObjectDataValidation(avocado.Test):
    """
    Test Class Description:
        Tests that create Different length records,
        Disconnect the pool/container and reconnect,
        validate the data after reconnect.
    """
    # pylint: disable=too-many-instance-attributes
    def setUp(self):
        self.agent_sessions = None
        self.pool = None
        self.container = None
        self.obj = None
        self.ioreq = None
        self.hostlist = None
        self.hostfile = None
        self.no_of_dkeys = None
        self.no_of_akeys = None
        self.array_size = None
        self.record_length = None

        with open('../../../.build_vars.json') as json_f:
            build_paths = json.load(json_f)
        basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        server_group = self.params.get("name",
                                       '/server_config/',
                                       'daos_server')
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.d_log = DaosLog(self.context)
        self.hostlist = self.params.get("test_machines", '/run/hosts/*')
        self.hostfile = write_host_file.write_host_file(self.hostlist,
                                                        self.workdir)
        self.no_of_dkeys = self.params.get("no_of_dkeys", '/run/dkeys/*')[0]
        self.no_of_akeys = self.params.get("no_of_akeys", '/run/akeys/*')[0]
        self.array_size = self.params.get("size", '/array_size/')
        self.record_length = self.params.get("length", '/run/record/*')

        self.agent_sessions = agent_utils.run_agent(basepath, self.hostlist)
        server_utils.run_server(self.hostfile, server_group, basepath)

        self.pool = DaosPool(self.context)
        self.pool.create(self.params.get("mode", '/run/pool/createmode/*'),
                         os.geteuid(),
                         os.getegid(),
                         self.params.get("size", '/run/pool/createsize/*'),
                         self.params.get("setname", '/run/pool/createset/*'),
                         None)
        self.pool.connect(2)

        self.container = DaosContainer(self.context)
        self.container.create(self.pool.handle)
        self.container.open()

        self.obj = DaosObj(self.context, self.container)
        self.obj.create(objcls=1)
        self.obj.open()
        self.ioreq = IORequest(self.context,
                               self.container,
                               self.obj, objtype=4)

    def tearDown(self):
        try:
            if self.container:
                self.container.close()
                self.container.destroy()
            if self.pool:
                self.pool.disconnect()
                self.pool.destroy(1)
        finally:
            if self.agent_sessions:
                agent_utils.stop_agent(self.agent_sessions)
            server_utils.stop_server(hosts=self.hostlist)

    def reconnect(self):
        '''
        Function to reconnect the pool/container and reopen the Object
        for read verification.
        '''
        #Close the Obj/Container, Disconnect the Pool.
        self.obj.close()
        self.container.close()
        self.pool.disconnect()
        time.sleep(5)
        #Connect Pool, Open Container and Object
        self.pool.connect(2)
        self.container.open()
        self.obj.open()
        self.ioreq = IORequest(self.context,
                               self.container,
                               self.obj,
                               objtype=4)

    @avocado.fail_on(DaosApiError)
    def test_invalid_tx_commit_close(self):
        """
        Test ID:
            (1)DAOS-1346: Verify commit tx bad parameter behavior.
            (2)DAOS-1343: Verify tx_close bad parameter behavior.
            (3)DAOS-1342: Verify tx_close through daos_api.
        Test Description:
            Write Avocado Test to verify commit tx and close tx
                          bad parameter behavior.
        :avocado: tags=all,pr,small,negative_test,neg_tx_commit,medium,vm
        """
        self.d_log.info("==Writing the Single Dataset for negative test...")
        record_index = 0
        expected_error = "RC: -1002"
        dkey = 0
        akey = 0
        indata = ("{0}".format(str(akey)[0])
                  * self.record_length[record_index])
        c_dkey = ctypes.create_string_buffer("dkey {0}".format(dkey))
        c_akey = ctypes.create_string_buffer("akey {0}".format(akey))
        c_value = ctypes.create_string_buffer(indata)
        c_size = ctypes.c_size_t(ctypes.sizeof(c_value))
        try:
            new_transaction = self.container.get_new_tx()
        except DaosApiError as excep:
            #initial container get_new_tx failed, skip rest of the test
            self.fail("##container get_new_tx failed: {}".format(excep))
        invalid_transaction = new_transaction + random.randint(1000, 383838)
        self.log.info("==new_transaction=     %s", new_transaction)
        self.log.info("==invalid_transaction= %s", invalid_transaction)
        self.ioreq.single_insert(c_dkey, c_akey, c_value, c_size,
                                 new_transaction)
        try:
            self.container.commit_tx(invalid_transaction)
            self.fail(
                "##(1.1)Container.commit_tx passing with invalid handle")
        except DaosApiError as excep:
            self.log.info(str(excep))
            self.log.info(
                "==(1)Expecting failure: invalid Container.commit_tx.")
            if expected_error not in str(excep):
                self.fail(
                    "##(1.2)Expecting error RC: -1002, but got {}."
                    .format(str(excep)))
        try:
            self.container.close_tx(invalid_transaction)
            self.fail(
                "##(2.1)Container.close_tx passing with invalid handle")
        except DaosApiError as excep:
            self.log.info(str(excep))
            self.log.info(
                "==(2)Expecting failure: invalid Container.commit_tx.")
            if expected_error not in str(excep):
                self.fail(
                    "##(2.2)Expecting error RC: -1002, but got {}."
                    .format(str(excep)))
        try:
            self.container.close_tx(new_transaction)
            self.log.info("==(3)container.close_tx test passed.")
        except DaosApiError as excep:
            self.log.info(str(excep))
            self.fail("##(3)Failed on close_tx.")


    @avocado.fail_on(DaosApiError)
    def test_single_object_validation(self):
        """
        Test ID: DAOS-707
        Test Description: Write Avocado Test to verify single data after
                          pool/container disconnect/reconnect.
        :avocado: tags=all,object,full_regression,small,single_object,
        """
        self.d_log.info("Writing the Single Dataset")
        record_index = 0
        transaction = []
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = ("{0}".format(str(akey)[0])
                          * self.record_length[record_index])
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(dkey))
                c_akey = ctypes.create_string_buffer("akey {0}".format(akey))
                c_value = ctypes.create_string_buffer(indata)
                c_size = ctypes.c_size_t(ctypes.sizeof(c_value))

                new_transaction = self.container.get_new_tx()
                self.ioreq.single_insert(c_dkey, c_akey, c_value, c_size,
                                         new_transaction)
                self.container.commit_tx(new_transaction)
                transaction.append(new_transaction)
                record_index = record_index + 1
                if record_index == len(self.record_length):
                    record_index = 0

        self.reconnect()

        self.d_log.info("Single Dataset Verification -- Started")
        record_index = 0
        transaction_index = 0
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = ("{0}".format(str(akey)[0]) *
                          self.record_length[record_index])
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(dkey))
                c_akey = ctypes.create_string_buffer("akey {0}".format(akey))
                val = self.ioreq.single_fetch(c_dkey,
                                              c_akey,
                                              len(indata)+1)
                if indata != (repr(val.value)[1:-1]):
                    self.d_log.error("ERROR:Data mismatch for "
                                     "dkey = {0}, "
                                     "akey = {1}".format(
                                         "dkey {0}".format(dkey),
                                         "akey {0}".format(akey)))
                    self.fail("ERROR: Data mismatch for dkey = {0}, akey={1}"
                              .format("dkey {0}".format(dkey),
                                      "akey {0}".format(akey)))

                transaction_index = transaction_index + 1
                record_index = record_index + 1
                if record_index == len(self.record_length):
                    record_index = 0

    @avocado.fail_on(DaosApiError)
    def test_array_object_validation(self):
        """
        Test ID: DAOS-707
        Test Description: Write Avocado Test to verify Array data after
                          pool/container disconnect/reconnect.
        :avocado: tags=all,object,full_regression,small,array_object
        """
        self.d_log.info("Writing the Array Dataset")
        record_index = 0
        transaction = []
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                c_values = []
                value = ("{0}".format(str(akey)[0])
                         * self.record_length[record_index])
                for item in range(self.array_size):
                    c_values.append((ctypes.create_string_buffer(value),
                                     len(value)+1))
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(dkey))
                c_akey = ctypes.create_string_buffer("akey {0}".format(akey))

                new_transaction = self.container.get_new_tx()
                self.ioreq.insert_array(c_dkey, c_akey, c_values,
                                        new_transaction)
                self.container.commit_tx(new_transaction)
                transaction.append(new_transaction)

                record_index = record_index + 1
                if record_index == len(self.record_length):
                    record_index = 0

        self.reconnect()

        self.d_log.info("Array Dataset Verification -- Started")
        record_index = 0
        transaction_index = 0
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = []
                value = ("{0}".format(str(akey)[0])
                         * self.record_length[record_index])
                for item in range(self.array_size):
                    indata.append(value)
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(dkey))
                c_akey = ctypes.create_string_buffer("akey {0}".format(akey))
                c_rec_count = ctypes.c_uint(len(indata))
                c_rec_size = ctypes.c_size_t(len(indata[0]) + 1)

                outdata = self.ioreq.fetch_array(c_dkey,
                                                 c_akey,
                                                 c_rec_count,
                                                 c_rec_size)

                for item in enumerate(indata):
                    if indata[item[0]] != outdata[item[0]][:-1]:
                        self.d_log.error("ERROR:Data mismatch for "
                                         "dkey = {0}, "
                                         "akey = {1}".format(
                                             "dkey {0}".format(dkey),
                                             "akey {0}".format(akey)))
                        self.fail("ERROR:Data mismatch for dkey = {0}, akey={1}"
                                  .format("dkey {0}".format(dkey),
                                          "akey {0}".format(akey)))

                transaction_index = transaction_index + 1
                record_index = record_index + 1
                if record_index == len(self.record_length):
                    record_index = 0
