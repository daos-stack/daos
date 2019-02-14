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

import os
import sys
import json
import traceback
import uuid
import threading
import Queue
import avocado

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import AgentUtils
import ServerUtils
import WriteHostFile
import IorUtils

from daos_api import DaosContext, DaosPool, DaosContainer, DaosApiError, DaosLog
NO_OF_MAX_CONTAINER = 13180

def ior_runner_thread(result_queue, tname, operation, **kwargs):
    """
    IOR Run thread method
    Args:
        result_queue: To have the result(PASS/FAIL) in the queue
        tname: Thread name
        operation: IOR Write/Read operation
        kwargs: Dictionary contain the IOR parameters
    """
    ior_flag = ""
    cont_uuid = ""
    ior_args = kwargs
    for no_file in range(ior_args['files_per_thread']):
        if operation == "write":
            with open("/{0}/{1}".format(ior_args['tmp_dir'],
                                        tname), "a") as contfile:
                cont_uuid = str(uuid.uuid4())
                contfile.write(cont_uuid + "\n")
            ior_flag = ior_args['iorwriteflags']
        elif operation == "read":
            filep = open("/{0}/{1}".format(ior_args['tmp_dir'],
                                           tname))
            lines = filep.readlines()
            ior_flag = ior_args['iorreadflags']
            cont_uuid = lines[no_file].rstrip()

        try:
            IorUtils.run_ior(ior_args['client_hostfile'],
                             ior_flag,
                             ior_args['iteration'],
                             ior_args['stripe_size'],
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
                             filename=cont_uuid,
                             display_output=False)
            result_queue.put("PASS")
        except Exception as exe:
            result_queue.put("FAIL")
            print("--- FAIL --- {0} Failed to run IOR {1} Exception {2}"
                  .format(tname,
                          no_file,
                          str(exe)))
            return

class ObjectMetadata(avocado.Test):
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
        self.pool_connect = True

        with open('../../../.build_vars.json') as json_f:
            build_paths = json.load(json_f)

        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        self.server_group = self.params.get("server_group",
                                            '/server/',
                                            'daos_server')
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.d_log = DaosLog(self.context)
        self.hostlist = self.params.get("servers", '/run/hosts/*')
        self.hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.workdir)
        self.hostlist_clients = self.params.get("clients", '/run/hosts/*')
        self.hostfile_clients = WriteHostFile.WriteHostFile(
            self.hostlist_clients, self.workdir
        )
        AgentUtils.run_agent(self.basepath, self.hostlist,
                             self.hostlist_clients)
        ServerUtils.runServer(self.hostfile, self.server_group, self.basepath)

        self.pool = DaosPool(self.context)
        self.pool.create(self.params.get("mode", '/run/pool/createmode/*'),
                         os.geteuid(),
                         os.getegid(),
                         self.params.get("size", '/run/pool/createsize/*'),
                         self.params.get("setname", '/run/pool/createset/*'),
                         nvme_size=self.params.get("size",
                                                   '/run/pool/nvmesize/*'))

    def tearDown(self):
        try:
            if self.pool_connect:
                self.pool.disconnect()
            if self.pool:
                self.pool.destroy(1)
        finally:
            AgentUtils.stop_agent(self.hostlist_clients)
            ServerUtils.stopServer(hosts=self.hostlist)

    @avocado.skip("Skipping until DAOS-1936/DAOS-1946 is fixed.")
    def test_metadata_fillup(self):
        """
        Test ID: DAOS-1512
        Test Description: Test to verify no IO happens after metadata is full.
        :avocado: tags=metadata,metadata_fill,nvme,small
        """
        self.pool.connect(2)
        container = DaosContainer(self.context)
        self.d_log.debug("Fillup Metadata....")
        for _cont in range(NO_OF_MAX_CONTAINER):
            container.create(self.pool.handle)
        self.d_log.debug("Metadata Overload...")
        #This should fail with no Metadata space Error.
        try:
            for _cont in range(250):
                container.create(self.pool.handle)
        except DaosApiError as exe:
            print (exe, traceback.format_exc())
            return

        self.fail("Test was expected to fail but it passed.\n")

    @avocado.skip("Skipping until DAOS-1965 is fixed.")
    @avocado.fail_on(DaosApiError)
    def test_metadata_addremove(self):
        """
        Test ID: DAOS-1512
        Test Description: Verify metadata release the space
                          after container delete.
        :avocado: tags=metadata,metadata_free_space,nvme,small
        """
        self.pool.connect(2)
        for k in range(10):
            container_array = []
            self.d_log.debug("Container Create Iteration {}".format(k))
            for cont in range(NO_OF_MAX_CONTAINER):
                container = DaosContainer(self.context)
                container.create(self.pool.handle)
                container_array.append(container)
            self.d_log.debug("Container Remove Iteration {} ".format(k))
            for cont in container_array:
                cont.destroy()

    def thread_control(self, threads, operation):
        """
        Start threads and wait till all threads execution is finished.
        It check queue for "FAIL" message and fail the avocado test.
        """
        self.d_log.debug("IOR {0} Threads Started -----".format(operation))
        for thrd in threads:
            thrd.start()
        for thrd in threads:
            thrd.join()

        while not self.out_queue.empty():
            if self.out_queue.get() is "FAIL":
                return "FAIL"
        self.d_log.debug("IOR {0} Threads Finished -----".format(operation))
        return "PASS"

    @avocado.fail_on(DaosApiError)
    def test_metadata_server_restart(self):
        """
        Test ID: DAOS-1512
        Test Description: This test will verify 2000 IOR small size container after server restart.
                          Test will write IOR in 5 different threads for faster execution time.
                          Each thread will create 400 (8bytes) containers to the same pool.
                          Restart the servers, read IOR container file written previously and
                          validate data integrity by using IOR option "-R -G 1".
        :avocado: tags=metadata,metadata_ior,nvme,small
        """
        self.pool_connect = False
        files_per_thread = 400
        total_ior_threads = 5
        threads = []
        ior_args = {}

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
        ior_args['iorwriteflags'] = self.params.get("F",
                                                    '/run/ior/iorwriteflags/')
        ior_args['iorreadflags'] = self.params.get("F",
                                                   '/run/ior/iorreadflags/')
        ior_args['iteration'] = self.params.get("iter", '/run/ior/iteration/')
        ior_args['stripe_size'] = self.params.get("s", '/run/ior/stripesize/*')
        ior_args['stripe_count'] = self.params.get("c", '/run/ior/stripecount/')
        ior_args['async_io'] = self.params.get("a", '/run/ior/asyncio/')
        ior_args['object_class'] = self.params.get("o", '/run/ior/objectclass/')
        ior_args['slots'] = self.params.get("slots", '/run/ior/clientslots/*')

        ior_args['files_per_thread'] = files_per_thread
        self.out_queue = Queue.Queue()

        #IOR write threads
        for i in range(total_ior_threads):
            threads.append(threading.Thread(target=ior_runner_thread,
                                            args=(self.out_queue,
                                                  "Thread-{}".format(i),
                                                  "write"),
                                            kwargs=ior_args))
        if self.thread_control(threads, "write") == "FAIL":
            self.d_log.error(" IOR write Thread FAIL")
            self.fail(" IOR write Thread FAIL")

        #Server Restart
        AgentUtils.stop_agent(self.hostlist_clients)
        ServerUtils.stopServer(hosts=self.hostlist)
        AgentUtils.run_agent(self.basepath, self.hostlist_clients,
                             self.hostlist)
        ServerUtils.runServer(self.hostfile, self.server_group, self.basepath)

        #Read IOR with verification with same number of threads
        threads = []
        for i in range(total_ior_threads):
            threads.append(threading.Thread(target=ior_runner_thread,
                                            args=(self.out_queue,
                                                  "Thread-{}".format(i),
                                                  "read"),
                                            kwargs=ior_args))
        if self.thread_control(threads, "read") == "FAIL":
            self.d_log.error(" IOR write Thread FAIL")
            self.fail(" IOR read Thread FAIL")
