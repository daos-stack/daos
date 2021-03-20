"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError
from general_utils import get_random_bytes


class FullPoolContainerCreate(TestWithServers):
    """
    Class for test to create a container in a pool with no remaining free space.
    :avocado: recursive
    """

    def test_no_space_cont_create(self):
        """
        :avocado: tags=all,container,tiny,full_regression,fullpoolcontcreate
        """
        # full storage rc
        err = "-1007"
        # probably should be -1007, revisit later
        err2 = "-1009"

        # create pool and connect
        self.prepare_pool()

        # query the pool
        self.log.info("Pool Query before write")
        self.pool.set_query_data()
        self.log.info(
            "Pool %s query data: %s\n", self.pool.uuid, self.pool.query_data)

        # create a container
        try:
            self.log.info("creating container 1")
            cont = DaosContainer(self.context)
            cont.create(self.pool.pool.handle)
            self.log.info("created container 1")
        except DaosApiError as excep:
            self.log.error("caught exception creating container: "
                           "%s", excep)
            self.fail("caught exception creating container: {}".format(excep))

        self.log.info("opening container 1")
        cont.open()

        # generate random dkey, akey each time
        # write 1mb until no space, then 1kb, etc. to fill pool quickly
        for obj_sz in [1048576, 10240, 10, 1]:
            write_count = 0
            while True:
                self.d_log.debug("writing obj {0} sz {1} to "
                                 "container".format(write_count, obj_sz))
                my_str = b"a" * obj_sz
                my_str_sz = obj_sz
                dkey = get_random_bytes(5)
                akey = get_random_bytes(5)
                try:
                    dummy_oid = cont.write_an_obj(
                        my_str, my_str_sz, dkey, akey, obj_cls="OC_SX")
                    self.d_log.debug("wrote obj {0}, sz {1}".format(write_count,
                                                                    obj_sz))
                    write_count += 1
                except DaosApiError as excep:
                    if not (err in repr(excep) or err2 in repr(excep)):
                        self.log.error("caught exception while writing "
                                       "object: %s", repr(excep))
                        cont.close()
                        self.fail("caught exception while writing "
                                  "object: {}".format(repr(excep)))
                    else:
                        self.log.info("pool is too full for %s byte "
                                      "objects", obj_sz)
                        break

        self.log.info("closing container")
        cont.close()

        # query the pool
        self.log.info("Pool Query after filling")
        self.pool.set_query_data()
        self.log.info(
            "Pool %s query data: %s\n", self.pool.uuid, self.pool.query_data)

        # create a 2nd container now that pool is full
        try:
            self.log.info("creating 2nd container")
            cont2 = DaosContainer(self.context)
            cont2.create(self.pool.pool.handle)
            self.log.info("created 2nd container")

            self.log.info("opening container 2")
            cont2.open()

            self.log.info("writing one more object, write expected to fail")
            cont2.write_an_obj(my_str, my_str_sz, dkey, akey,
                               obj_cls="OC_SX")
            self.log.info("closing container")
            cont2.close()
            self.fail("wrote one more object after pool was completely filled,"
                      " this should never print")
        except DaosApiError as excep:
            if not (err in repr(excep) or err2 in repr(excep)):
                self.log.error("caught unexpected exception while "
                               "writing object: %s", repr(excep))
                self.log.info("closing container")
                cont2.close()
                self.fail("caught unexpected exception while writing "
                          "object: {}".format(repr(excep)))
            else:
                self.log.info("correctly caught -1007 while attempting "
                              "to write object in full pool")
                self.log.info("closing container")
                cont2.close()
