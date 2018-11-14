#!/usr/bin/python2
'''
  (C) Copyright 2018 Intel Corporation.

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
import time
import traceback
import sys
import json
import threading
from apricot       import Test
from avocado       import main
from avocado.utils import process


import ServerUtils
import CheckForPool
import WriteHostFile
from daos_api import DaosContainer, DaosContext, DaosPool, DaosApiError
from conversion import c_uuid_to_str

GLOB_SIGNAL = None
GLOB_RC = -99000000

def cb_func(event):
    global GLOB_SIGNAL
    global GLOB_RC

    GLOB_RC = event.event.ev_error
    GLOB_SIGNAL.set()

class DestroyTests(Test):
    """
    Tests DAOS pool removal
    :avocado: recursive
    """
    # super wasteful since its doing this for every variation
    def setUp(self):
        # there is a presumption that this test lives in a specific
        # spot in the repo
        with open('../../../.build_vars.json') as f:
            build_paths = json.load(f)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        self.tmp = build_paths['PREFIX'] + '/tmp'

        self.server_group = self.params.get("server_group",
                                            '/server/',
                                            'daos_server')

        # setup the DAOS python API
        self.Context = DaosContext(build_paths['PREFIX'] + '/lib/')

    def test_simple_delete(self):
        """
        Test destroying a pool created on a single server, nobody is using
        the pool, force is not needed.

        :avocado: tags=pool,pooldestroy,quick
        """
        self.hostlist = self.params.get("test_machines1", '/run/hosts/')
        hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

        ServerUtils.runServer(hostfile, self.server_group, self.basepath)

        setid = self.params.get("setname",
                                '/run/setnames/validsetname/')

        try:
            # use the uid/gid of the user running the test, these should
            # be perfectly valid
            uid = os.geteuid()
            gid = os.getegid()

            # TODO make these params in the yaml
            daosctl = self.basepath + '/install/bin/daosctl'

            create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'
                          .format(daosctl, 0x731, uid, gid, setid))

            uuid_str = """{0}""".format(process.system_output(create_cmd))
            print ("uuid is {0}\n".format(uuid_str))

            host = self.hostlist[0]
            exists = CheckForPool.checkForPool(host, uuid_str)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n"
                          .format(uuid_str, host))

            delete_cmd = ('{0} destroy-pool -i {1} -s {2}'
                          .format(daosctl, uuid_str, setid))
            process.system(delete_cmd)

            exists = CheckForPool.checkForPool(host, uuid_str)
            if exists == 0:
                self.fail("Pool {0} found on host {1} when not expected.\n"
                          .format(uuid_str, host))

        except Exception as e:
            print (e)
            print (traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")

        # no matter what happens shutdown the server
        finally:
            try:
                os.remove(hostfile)
            finally:
                ServerUtils.stopServer(hosts=self.hostlist)

    def test_delete_doesnt_exist(self):
        """
        Test destroying a pool uuid that doesn't exist.

        :avocado: tags=pool,pooldestroy
        """
        self.hostlist = self.params.get("test_machines1", '/run/hosts/')
        hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

        ServerUtils.runServer(hostfile, self.server_group, self.basepath)

        setid = self.params.get("setname",
                                '/run/setnames/validsetname/')

        try:
            # randomly selected uuid, that is exceptionally unlikely to exist
            bogus_uuid = '81ef94d7-a59d-4a5e-935b-abfbd12f2105'

            # TODO make these params in the yaml
            daosctl = self.basepath + '/install/bin/daosctl'

            delete_cmd = ('{0} destroy-pool -i {1} -s {2}'.format(daosctl,
                                                                  bogus_uuid,
                                                                  setid))

            process.system(delete_cmd)

            # the above command should fail resulting in an exception so if
            # we get here the test has failed
            self.fail("Pool {0} found on host {1} when not expected.\n"
                      .format(uuid_str, host))

        except Exception as e:
            # expecting an exception so catch and pass the test
            pass

        # no matter what happens shutdown the server
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)
            os.remove(hostfile)


    def test_delete_wrong_servers(self):
        """
        Test destroying a pool valid pool but use the wrong server group.

        :avocado: tags=pool,pooldestroy
        """

        self.hostlist = self.params.get("test_machines1", '/run/hosts/')
        hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

        ServerUtils.runServer(hostfile, self.server_group, self.basepath)

        # need both a good and bad set
        goodsetid = self.params.get("setname",
                                    '/run/setnames/validsetname/')

        badsetid = self.params.get("setname",
                                   '/run/setnames/badsetname/')

        uuid_str = ""

        # TODO make these params in the yaml
        daosctl = self.basepath + '/install/bin/daosctl'

        try:
            # use the uid/gid of the user running the test, these should
            # be perfectly valid
            uid = os.geteuid()
            gid = os.getegid()

            create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'
                          .format(daosctl, 0x731, uid, gid, goodsetid))
            uuid_str = """{0}""".format(process.system_output(create_cmd))
            print ("uuid is {0}\n".format(uuid_str))

            exists = CheckForPool.checkForPool(host, uuid_str)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n"
                          .format(uuid_str, host))

            delete_cmd = ('{0} destroy-pool -i {1} -s {2}'.format(daosctl,
                                                                  uuid_str,
                                                                  badsetid))

            process.system(delete_cmd)

            # the above command should fail resulting in an exception so if
            # we get here the test has failed
            self.fail("Pool {0} found on host {1} when not expected.\n"
                      .format(uuid_str, host))

        except Exception as e:
            # expecting an exception, but now need to
            # clean up the pool for real
            delete_cmd = ('{0} destroy-pool -i {1} -s {2}'
                          .format(daosctl, uuid_str, goodsetid))
            process.system(delete_cmd)

        # no matter what happens shutdown the server
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)
            os.remove(hostfile)


    def test_multi_server_delete(self):
        """
        Test destroying a pool created on two servers, nobody is using
        the pool, force is not needed.  This is accomplished by switching
        hostfiles.

        :avocado: tags=pool,pooldestroy,multiserver
        """
        self.hostlist = self.params.get("test_machines2", '/run/hosts/')
        hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

        ServerUtils.runServer(hostfile, self.server_group, self.basepath)

        setid = self.params.get("setname",
                                '/run/setnames/validsetname/')

        # TODO make these params in the yaml
        daosctl = self.basepath + '/install/bin/daosctl'

        try:
            # use the uid/gid of the user running the test, these should
            # be perfectly valid
            uid = os.geteuid()
            gid = os.getegid()

            create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'
                          .format(daosctl, 0x731, uid, gid, setid))
            uuid_str = """{0}""".format(process.system_output(create_cmd))
            print ("uuid is {0}\n".format(uuid_str))

            exists = CheckForPool.checkForPool(self.hostlist[0], uuid_str)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n"
                          .format(uuid_str, self.hostlist[0]))
                exists = CheckForPool.checkForPool(self.hostlist[1], uuid_str)
                if exists != 0:
                    self.fail("Pool {0} not found on host {1}.\n"
                              .format(uuid_str, host2))

                delete_cmd = ('{0} destroy-pool -i {1} -s {2}'
                              .format(daosctl, uuid_str, setid))
                process.system(delete_cmd)

                exists = CheckForPool.checkForPool(self.hostlist[0], uuid_str)
                if exists == 0:
                    self.fail("Pool {0} found on host {1} when not"
                              " expected.\n".format(uuid_str, self.hostlist[0]))
                exists = CheckForPool.checkForPool(self.hostlist[1], uuid_str)
                if exists == 0:
                    self.fail("Pool {0} found on host {1} when not "
                              "expected.\n".format(uuid_str, self.hostlist[1]))

        except Exception as e:
            print (e)
            print (traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")

        # no matter what happens shutdown the server
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)
            os.remove(hostfile)

    def test_bad_server_group(self):
        """
        Test destroying a pool created on server group A by passing
        in server group B, should fail.

        :avocado: tags=pool,pooldestroy
        """

        setid2 = self.basepath + self.params.get("setname",
                                                 '/run/setnames/othersetname/')

        self.hostlist1 = self.params.get("test_machines1", '/run/hosts/')
        hostfile1 = WriteHostFile.WriteHostFile(self.hostlist1, self.tmp)

        self.hostlist2 = self.params.get("test_machines2a", '/run/hosts/')
        hostfile2 = WriteHostFile.WriteHostFile(self.hostlist2, self.tmp)


        # TODO make these params in the yaml
        daosctl = self.basepath + '/install/bin/daosctl'

        # start 2 different sets of servers,
        ServerUtils.runServer(hostfile1, self.server_group, self.basepath)
        ServerUtils.runServer(hostfile2, setid2, self.basepath)

        host = self.hostlist1[0]

        uuid_str = ""

        try:
            # use the uid/gid of the user running the test, these should
            # be perfectly valid
            uid = os.geteuid()
            gid = os.getegid()

            create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'
                          .format(daosctl, 0x731, uid, gid,
                                  self.server_group))
            uuid_str = """{0}""".format(process.system_output(create_cmd))
            print ("uuid is {0}\n".format(uuid_str))

            exists = CheckForPool.checkForPool(host, uuid_str)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n"
                          .format(uuid_str, host))

            # try and delete it using the wrong group
            delete_cmd = ('{0} destroy-pool -i {1} -s {2}'
                          .format(daosctl, uuid_str, setid2))

            process.system(delete_cmd)

            exists = CheckForPool.checkForPool(host, uuid_str)
            if exists != 0:
                self.fail("Pool {0} not found on host {1} but delete "
                          "should have failed.\n".format(uuid_str, host))

        except Exception as e:
            # now issue a good delete command so we clean-up after this test
            delete_cmd = ('{0} destroy-pool -i {1} -s {2}'
                          .format(daosctl, uuid_str, self.server_group))

            process.system(delete_cmd)

            exists = CheckForPool.checkForPool(host, uuid_str)
            if exists == 0:
                self.fail("Pool {0} ound on host {1} but delete"
                          "should have removed it.\n"
                          .format(uuid_str, host))

        # no matter what happens shutdown the server
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)
            os.remove(hostfile1)
            os.remove(hostfile2)

    def test_destroy_connect(self):
        """
        Test destroying a pool that has a connected client with force == false.
        Should fail.

        :avocado: tags=pool,pooldestroy,x
        """

        try:

            # write out a hostfile and start the servers with it
            self.hostlist = self.params.get("test_machines1", '/run/hosts/')
            hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

            ServerUtils.runServer(hostfile, self.server_group, self.basepath)

            # parameters used in pool create
            createmode = self.params.get("mode", '/run/poolparams/createmode/')
            createuid = self.params.get("uid", '/run/poolparams/createuid/')
            creategid = self.params.get("gid", '/run/poolparams/creategid/')
            createsetid = self.params.get("setname",
                                          '/run/poolparams/createset/')
            createsize = self.params.get("size", '/run/poolparams/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            POOL = DaosPool(self.Context)
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # need a connection to create container
            POOL.connect(1 << 1)

            # destroy pool with connection open
            POOL.destroy(0)

            # should throw an exception and not hit this
            self.fail("Shouldn't hit this line.\n")

        except DaosApiError as e:
            print ("got exception which is expected so long as it is BUSY")
            print (e)
            print (traceback.format_exc())
            # pool should still be there
            exists = CheckForPool.checkForPool(host, uuid_str)
            if exists != 0:
                self.fail("Pool gone, but destroy should have failed.\n")

        # no matter what happens cleanup
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)
            os.remove(hostfile)

    def test_destroy_recreate(self):
        """
        Test destroy and recreate one right after the other multiple times
        Should fail.

        :avocado: tags=pool,pooldestroy,destroyredo
        """

        try:
            # write out a hostfile and start the servers with it
            self.hostlist = self.params.get("test_machines1", '/run/hosts/')
            hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

            ServerUtils.runServer(hostfile, self.server_group, self.basepath)

            # parameters used in pool create
            createmode = self.params.get("mode", '/run/poolparams/createmode/')
            createuid = self.params.get("uid", '/run/poolparams/createuid/')
            creategid = self.params.get("gid", '/run/poolparams/creategid/')
            createsetid = self.params.get("setname",
                                          '/run/poolparams/createset/')
            createsize = self.params.get("size", '/run/poolparams/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            POOL = DaosPool(self.Context)
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # blow it away immediately
            POOL.destroy(1)

            # now recreate
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # blow it away immediately
            POOL.destroy(1)

            # now recreate
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # blow it away immediately
            POOL.destroy(1)

        except DaosApiError as e:
            print (e)
            print (traceback.format_exc())
            self.fail("create/destroy/create/destroy test failed.\n")

        except Exception as e:
            self.fail("Daos code segfaulted most likely.  Error: %s" % e)

        # no matter what happens cleanup
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)
            os.remove(hostfile)

    def test_many_servers(self):
        """
        Test destroy on a large (relative) number of servers.

        :avocado: tags=pool,pooldestroy,destroybig
        """
        try:
            # write out a hostfile and start the servers with it
            self.hostlist = self.params.get("test_machines6", '/run/hosts/')
            hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

            ServerUtils.runServer(hostfile, self.server_group, self.basepath)

            # parameters used in pool create
            createmode = self.params.get("mode", '/run/poolparams/createmode/')
            createuid = self.params.get("uid", '/run/poolparams/createuid/')
            creategid = self.params.get("gid", '/run/poolparams/creategid/')
            createsetid = self.params.get("setname",
                                          '/run/poolparams/createset/')
            createsize = self.params.get("size", '/run/poolparams/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            POOL = DaosPool(self.Context)
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            time.sleep(1)

            # okay, get rid of it
            POOL.destroy(1)

        except DaosApiError as e:
            print (e)
            print (traceback.format_exc())
            self.fail("6 server test failed.\n")

        except Exception as e:
            self.fail("Daos code segfaulted most likely.  Error: %s" % e)

        # no matter what happens cleanup
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)
            os.remove(hostfile)

    def test_destroy_withdata(self):
        """
        Test destroy and recreate one right after the other multiple times
        Should fail.

        :avocado: tags=pool,pooldestroy,destroydata
        """
        try:
            # write out a hostfile and start the servers with it
            self.hostlist = self.params.get("test_machines1", '/run/hosts/')
            hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

            ServerUtils.runServer(hostfile, self.server_group, self.basepath)

            # parameters used in pool create
            createmode = self.params.get("mode", '/run/poolparams/createmode/')
            createuid = self.params.get("uid", '/run/poolparams/createuid/')
            creategid = self.params.get("gid", '/run/poolparams/creategid/')
            createsetid = self.params.get("setname",
                                          '/run/poolparams/createset/')
            createsize = self.params.get("size", '/run/poolparams/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            POOL = DaosPool(self.Context)
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # need a connection to create container
            POOL.connect(1 << 1)

            # create a container
            CONTAINER = DaosContainer(self.Context)
            CONTAINER.create(POOL.handle)

            POOL.disconnect()

            daosctl = self.basepath + '/install/bin/daosctl'

            write_cmd = ('{0} write-pattern -i {1} -l 0 -c {2} -p sequential'.
                         format(daosctl, c_uuid_to_str(POOL.uuid),
                                c_uuid_to_str(CONTAINER.uuid)))

            process.system_output(write_cmd)

            # blow it away
            POOL.destroy(1)

        except DaosApiError as e:
            print (e)
            print (traceback.format_exc())
            self.fail("create/destroy/create/destroy test failed.\n")

        except Exception as e:
            self.fail("Daos code segfaulted most likely.  Error: %s" % e)

        # no matter what happens cleanup
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)
            os.remove(hostfile)

    def test_destroy_async(self):
        """
        Performn destroy asynchronously, successful and failed.

        :avocado: tags=pool,pooldestroy,destroyasync
        """

        global GLOB_SIGNAL
        global GLOB_RC

        try:
            # write out a hostfile and start the servers with it
            self.hostlist = self.params.get("test_machines1", '/run/hosts/')
            hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

            ServerUtils.runServer(hostfile, self.server_group, self.basepath)

            # parameters used in pool create
            createmode = self.params.get("mode", '/run/poolparams/createmode/')
            createuid = self.params.get("uid", '/run/poolparams/createuid/')
            creategid = self.params.get("gid", '/run/poolparams/creategid/')
            createsetid = self.params.get("setname",
                                          '/run/poolparams/createset/')
            createsize = self.params.get("size", '/run/poolparams/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            POOL = DaosPool(self.Context)
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # allow the callback to tell us when its been called
            GLOB_SIGNAL = threading.Event()

            # blow it away but this time get return code via callback function
            POOL.destroy(1, cb_func)

            # wait for callback
            GLOB_SIGNAL.wait()
            if GLOB_RC != 0:
                self.fail("RC not as expected in async test")

            # recreate the pool, reset the signal, shutdown the
            # servers so call will fail and then check rc in the callback
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)
            GLOB_SIGNAL = threading.Event()
            GLOB_RC = -9900000
            ServerUtils.stopServer(hosts=self.hostlist)
            POOL.destroy(1, cb_func)

            # wait for callback, expecting a timeout since servers are down
            GLOB_SIGNAL.wait()
            if GLOB_RC != -1011:
                self.fail("RC not as expected in async test")

        except DaosApiError as e:
            print (e)
            print (traceback.format_exc())
            self.fail("destroy async test failed.\n")

        except Exception as e:
            self.fail("Daos code segfaulted most likely. Error: %s" % e)

        # no matter what happens cleanup
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)
            os.remove(hostfile)