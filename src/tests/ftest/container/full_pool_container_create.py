"""
  (C) Copyright 2018-2019 Intel Corporation.

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
import random
import string

from apricot import TestWithServers

from daos_api import DaosContainer, DaosApiError

class FullPoolContainerCreate(TestWithServers):
    """
    Class for test to create a container in a pool with no remaining free space.
    :avocado: recursive
    """

    def tearDown(self):
        # shut 'er down
        """
        wrap pool destroy in a try; in case pool create didn't succeed, we
        still need the server to be shut down in any case
        """
        try:
            self.pool.destroy(1)
        finally:
            super(FullPoolContainerCreate, self).tearDown()

    def test_no_space_cont_create(self):
        """
        :avocado: tags=pool,cont,fullpoolcontcreate,small,vm
        """
        # full storage rc
        err = "-1007"
        # probably should be -1007, revisit later
        err2 = "-1009"

        # create pool
        mode = self.params.get("mode", '/conttests/createmode/')
        self.d_log.debug("mode is {0}".format(mode))
        uid = os.geteuid()
        gid = os.getegid()
        # 16 mb pool, minimum size currently possible
        size = 16777216

        self.d_log.debug("creating pool")
        self.pool.create(mode, uid, gid, size, self.server_group, None)
        self.d_log.debug("created pool")

        # connect to the pool
        self.d_log.debug("connecting to pool")
        self.pool.connect(1 << 1)
        self.d_log.debug("connected to pool")

        # query the pool
        self.d_log.debug("querying pool info")
        dummy_pool_info = self.pool.pool_query()
        self.d_log.debug("queried pool info")

        # create a container
        try:
            self.d_log.debug("creating container")
            self.cont = DaosContainer(self.context)
            self.cont.create(self.pool.handle)
            self.d_log.debug("created container")
        except DaosApiError as excep:
            self.d_log.error("caught exception creating container: "
                             "{0}".format(excep))
            self.fail("caught exception creating container: {0}".format(excep))

        self.d_log.debug("opening container")
        self.cont.open()
        self.d_log.debug("opened container")

        # generate random dkey, akey each time
        # write 1mb until no space, then 1kb, etc. to fill pool quickly
        for obj_sz in [1048576, 1024, 1]:
            write_count = 0
            while True:
                self.d_log.debug("writing obj {0}, sz {1} to "
                                 "container".format(write_count, obj_sz))
                my_str = "a" * obj_sz
                my_str_sz = obj_sz
                dkey = (
                    ''.join(random.choice(string.lowercase) for i in range(5)))
                akey = (
                    ''.join(random.choice(string.lowercase) for i in range(5)))
                try:
                    dummy_oid, dummy_tx = self.cont.write_an_obj(my_str,
                                                                 my_str_sz,
                                                                 dkey, akey,
                                                                 obj_cls=1)
                    self.d_log.debug("wrote obj {0}, sz {1}".format(write_count,
                                                                    obj_sz))
                    write_count += 1
                except DaosApiError as excep:
                    if not (err in repr(excep) or err2 in repr(excep)):
                        self.d_log.error("caught exception while writing "
                                         "object: {0}".format(repr(excep)))
                        self.fail("caught exception while writing object: {0}"
                                  .format(repr(excep)))
                    else:
                        self.d_log.debug("pool is too full for {0} byte "
                                         "objects".format(obj_sz))
                        break

        self.d_log.debug("closing container")
        self.cont.close()
        self.d_log.debug("closed container")
        # create a 2nd container now that pool is full
        try:
            self.d_log.debug("creating 2nd container")
            self.cont2 = DaosContainer(self.context)
            self.cont2.create(self.pool.handle)
            self.d_log.debug("created 2nd container")

            self.d_log.debug("opening container 2")
            self.cont2.open()
            self.d_log.debug("opened container 2")

            self.d_log.debug("writing one more object, write expected to fail")
            self.cont2.write_an_obj(my_str, my_str_sz, dkey, akey, obj_cls=1)
            self.d_log.debug("wrote one more object--this should never print")
        except DaosApiError as excep:
            if not (err in repr(excep) or err2 in repr(excep)):
                self.d_log.error("caught unexpected exception while "
                                 "writing object: {0}".format(repr(excep)))
                self.fail("caught unexpected exception while writing "
                          "object: {0}".format(repr(excep)))
            else:
                self.d_log.debug("correctly caught -1007 while attempting "
                                 "to write object in full pool")
