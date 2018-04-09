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

from avocado       import Test
from avocado       import main
from avocado.utils import process
from avocado.utils import git

import aexpect
from aexpect.client import run_bg

sys.path.append('./util')
import ServerUtils
import CheckForPool
import GetHostsFromFile


class DestroyTests(Test):
    """
    Tests DAOS pool removal

    :avocado: tags=pool,pooldestroy
    """

    # super wasteful since its doing this for every variation
    def setUp(self):
           global basepath
           global server_group

           # there is a presumption that this test lives in a specific
           # spot in the repo
           basepath = os.path.normpath(os.getcwd() + "../../../../")
           server_group = self.params.get("server_group",'/server/',
                                          'daos_server')

    def tearDown(self):
           pass

    def test_simple_delete(self):
        """
        Test destroying a pool created on a single server, nobody is using
        the pool, force is not needed.

        :avocado: tags=pool,pooldestroy,quick
        """
        global basepath
        global server_group

        hostfile = basepath
        hostfile += self.params.get("hostfile",'/run/testparams/one_host/')

        ServerUtils.runServer(hostfile, server_group, basepath)

        # not sure I need to do this but ... give it time to start
        time.sleep(1)

        setid = self.params.get("setname",
                                '/run/testparams/setnames/validsetname/')

        try:
               # use the uid/gid of the user running the test, these should
               # be perfectly valid
               uid = os.geteuid()
               gid = os.getegid()

               # TODO make these params in the yaml
               daosctl = basepath + '/install/bin/daosctl'

               create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'.
                             format(daosctl, 0x731, uid, gid, setid))

               uuid_str = """{0}""".format(process.system_output(create_cmd))
               print("uuid is {0}\n".format(uuid_str))

               host = GetHostsFromFile.getHostsFromFile(hostfile)[0]
               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".
                                format(uuid_str, host))

               delete_cmd =  ('{0} destroy-pool -i {1} -s {2}'.
                              format(daosctl, uuid_str, setid))
               process.system(delete_cmd)

               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} when not "
                                "expected.\n".format(uuid_str, host))

        except Exception as e:
               print(e)
               print(traceback.format_exc())
               self.fail("Expecting to pass but test has failed.\n")

        # no matter what happens shutdown the server
        finally:
               ServerUtils.stopServer()


    def test_delete_doesnt_exist(self):
        """
        Test destroying a pool uuid that doesn't exist.

        :avocado: tags=pool,pooldestroy
        """
        global basepath
        global server_group

        hostfile = basepath + self.params.get("hostfile",
                '/run/testparams/one_host/')


        ServerUtils.runServer(hostfile, server_group, basepath)

        # not sure I need to do this but ... give it time to start
        time.sleep(1)

        setid = self.params.get("setname",
                                '/run/testparams/setnames/validsetname/')

        try:
               # randomly selected uuid, that is exceptionally unlikely to exist
               bogus_uuid = '81ef94d7-a59d-4a5e-935b-abfbd12f2105'

               # TODO make these params in the yaml
               daosctl = basepath + '/install/bin/daosctl'

               delete_cmd =  ('{0} destroy-pool -i {1} -s {2}'.format(
                   daosctl, bogus_uuid, setid))

               process.system(delete_cmd)

               # the above command should fail resulting in an exception so if
               # we get here the test has failed
               self.fail("Pool {0} found on host {1} when not expected.\n".
                              format(uuid_str, host))

        except Exception as e:
               # expecting an exception so catch and pass the test
               pass;

        # no matter what happens shutdown the server
        finally:

               ServerUtils.stopServer()


    def test_delete_wrong_servers(self):
        """
        Test destroying a pool valid pool but use the wrong server group.

        :avocado: tags=pool,pooldestroy
        """
        global basepath
        global server_group

        hostfile = basepath + self.params.get("hostfile",
                              '/run/testparams/one_host/')

        ServerUtils.runServer(hostfile, server_group, basepath)

        # not sure I need to do this but ... give it time to start
        time.sleep(1)

        # need both a good and bad set
        goodsetid = self.params.get("setname",
                                '/run/testparams/setnames/validsetname/')

        badsetid = self.params.get("setname",
                                '/run/testparams/setnames/badsetname/')

        uuid_str = ""

        # TODO make these params in the yaml
        daosctl = basepath + '/install/bin/daosctl'

        try:
               # use the uid/gid of the user running the test, these should
               # be perfectly valid
               uid = os.geteuid()
               gid = os.getegid()

               create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'.
                      format(daosctl, 0x731, uid, gid, goodsetid))
               uuid_str = """{0}""".format(process.system_output(create_cmd))
               print("uuid is {0}\n".format(uuid_str))

               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".
                      format(uuid_str, host))

               delete_cmd =  ('{0} destroy-pool -i {1} -s {2}'.format(
                             daosctl, uuid_str, badsetid))

               process.system(delete_cmd)

               # the above command should fail resulting in an exception so if
               # we get here the test has failed
               self.fail("Pool {0} found on host {1} when not expected.\n".
                              format(uuid_str, host))

        except Exception as e:
               # expecting an exception, but now need to
               # clean up the pool for real
               delete_cmd = ('{0} destroy-pool -i {1} -s {2}'.
                             format(daosctl, uuid_str, goodsetid))
               process.system(delete_cmd)

        # no matter what happens shutdown the server
        finally:
               ServerUtils.stopServer()


    def test_multi_server_delete(self):
        """
        Test destroying a pool created on two servers, nobody is using
        the pool, force is not needed.  This is accomplished by switching
        hostfiles.

        :avocado: tags=pool,pooldestroy,multiserver
        """
        global basepath
        global server_group

        hostfile = basepath + self.params.get("hostfile",
                              '/run/testparams/two_hosts/')

        ServerUtils.runServer(hostfile, server_group, basepath)

        # not sure I need to do this but ... give it time to start
        time.sleep(1)

        setid = self.params.get("setname",
                                '/run/testparams/setnames/validsetname/')

        # TODO make these params in the yaml
        daosctl = basepath + '/install/bin/daosctl'

        host1 = GetHostsFromFile.getHostsFromFile(hostfile)[0]
        host2 = GetHostsFromFile.getHostsFromFile(hostfile)[1]

        try:
               # use the uid/gid of the user running the test, these should
               # be perfectly valid
               uid = os.geteuid()
               gid = os.getegid()

               create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'.
                             format(daosctl, 0x731, uid, gid, setid))
               uuid_str = """{0}""".format(process.system_output(create_cmd))
               print("uuid is {0}\n".format(uuid_str))

               exists = CheckForPool.checkForPool(host1, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".
                      format(uuid_str, host1))
               exists = CheckForPool.checkForPool(host2, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".
                      format(uuid_str, host2))

               delete_cmd =  ('{0} destroy-pool -i {1} -s {2}'.
                              format(daosctl, uuid_str, setid))
               process.system(delete_cmd)

               exists = CheckForPool.checkForPool(host1, uuid_str)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} when not"
                                " expected.\n".format(uuid_str, host1))
               exists = CheckForPool.checkForPool(host2, uuid_str)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} when not "
                                "expected.\n".format(uuid_str, host2))

        except Exception as e:
               print(e)
               print(traceback.format_exc())
               self.fail("Expecting to pass but test has failed.\n")

        # no matter what happens shutdown the server
        finally:
               ServerUtils.stopServer()


    def test_bad_server_group(self):
        """
        Test destroying a pool created on server group A by passing
        in server group B, should fail.

        :avocado: tags=pool,pooldestroy
        """

        global basepath
        global server_group

        # TODO make this a param in YAML
        setid2 = "other_server"

        hostfile1 = basepath + self.params.get("hostfile",
                              '/run/testparams/one_host/')
        hostfile2 = basepath + self.params.get("hostfile",
                              '/run/testparams/two_other_hosts/')

        # TODO make these params in the yaml
        daosctl = basepath + '/install/bin/daosctl'

        # start 2 different sets of servers,
        ServerUtils.runServer(hostfile1, server_group, basepath)
        ServerUtils.runServer(hostfile2, setid2, basepath)

        host = GetHostsFromFile.getHostsFromFile(hostfile1)[0]

        # not sure I need to do this but ... give it time to start
        time.sleep(1)

        uuid_str = ""

        try:
               # use the uid/gid of the user running the test, these should
               # be perfectly valid
               uid = os.geteuid()
               gid = os.getegid()

               create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'.
                             format(daosctl, 0x731, uid, gid, server_group))
               uuid_str = """{0}""".format(process.system_output(create_cmd))
               print("uuid is {0}\n".format(uuid_str))

               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".
                      format(uuid_str, host))

               # try and delete it using the wrong group
               delete_cmd =  ('{0} destroy-pool -i {1} -s {2}'.
                              format(daosctl, uuid_str, setid2))

               process.system(delete_cmd)

               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1} but delete "
                                "should have failed.\n".format(uuid_str, host))

        except Exception as e:

               # now issue a good delete command so we clean-up after this test
               delete_cmd =  ('{0} destroy-pool -i {1} -s {2}'.
                              format(uuid_str, server_group))

               process.system(delete_cmd)

               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists == 0:
                      self.fail("Pool {0} ound on host {1} but delete "
                                "should have removed it.\n".
                                format(uuid_str, host))

        # no matter what happens shutdown the server
        finally:
               ServerUtils.stopServer()

    # this test won't work as designed, the connection is dropped
    # when the daosctl connect call exits
    # renaming it temporarily until its reworked (renaming
    # keeps it from being called)
    def dontrun_test_destroy_connect(self):
        """
        Test destroying a pool that has a connected client with force == false.
        Should fail.

        :avocado: tags=pool,pooldestroy
        """
        global basepath
        global server_group

        hostfile = basepath + self.params.get("hostfile",
                                              '/run/testparams/one_host/')

        ServerUtils.runServer(hostfile, server_group, basepath)

        host = GetHostsFromFile.getHostsFromFile(hostfile)[0]

        # TODO make these params in the yaml
        daosctl = basepath + '/install/bin/daosctl'

        # not sure I need to do this but ... give it time to start
        time.sleep(1)

        uuid_str = ""
        failed = 0;
        try:
               uid = os.geteuid()
               gid = os.getegid()

               create_cmd = ('{0} create-pool -m {1} -u {2} -g {3} -s {4}'.
                             format(daosctl, 0x731, uid, gid, server_group))
               uuid_str = """{0}""".format(process.system_output(create_cmd))
               print("uuid is {0}\n".format(uuid_str))

               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".
                                format(uuid_str, host))

               connect_cmd = ('{0} connect-pool -i {1} -s {2} -r'.
                              format(daosctl, uuid_str, server_group))
               process.system(connect_cmd)

               delete_cmd =  ('{0} destroy-pool -i {1} -s {2}'.format(
                   daosctl, uuid_str, server_group))

               process.system(delete_cmd)

               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists == 0:
                      print("Didn't return the right code but also didn't "
                            "delete the pool")

               # should throw an exception and not hit this
               fail = 1
               self.fail("Shouldn't hit this line.\n")

        except Exception as e:
               print("got exception which is expected so long as it relates "
                     "to delete cmd")
               print(e)
               print(traceback.format_exc())

               # this time force = 1, should work
               delete_cmd =  ('{0} destroy-pool -i {1} -s {2} -f'.
                              format(daosctl, uuid_str, server_group))

               process.system(delete_cmd)

               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after delete.\n".
                                format(uuid_str, host))

               if fail == 1:
                      self.fail("Didn't return DER_BUSY.\n")

        # no matter what happens shutdown the server
        finally:
               ServerUtils.stopServer()

if __name__ == "__main__":
    main()
