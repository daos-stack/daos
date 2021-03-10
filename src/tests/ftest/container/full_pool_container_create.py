"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers, skipForTicket
from pydaos.raw import DaosContainer, DaosApiError
from general_utils import get_random_string


class FullPoolContainerCreate(TestWithServers):
    """
    Class for test to create a container in a pool with no remaining free space.
    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        super(FullPoolContainerCreate, self).__init__(*args, **kwargs)
        self.cont = None
        self.cont2 = None

    @skipForTicket("DAOS-3142")
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
        self.d_log.debug("querying pool info")
        self.pool.get_info()
        self.d_log.debug("queried pool info")

        # create a container
        try:
            self.d_log.debug("creating container")
            self.cont = DaosContainer(self.context)
            self.cont.create(self.pool.pool.handle)
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
        for obj_sz in [1048576, 10240, 10, 1]:
            write_count = 0
            while True:
                self.d_log.debug("writing obj {0}, sz {1} to "
                                 "container".format(write_count, obj_sz))
                my_str = "a" * obj_sz
                my_str_sz = obj_sz
                dkey = (get_random_string(5))
                akey = (get_random_string(5))
                try:
                    dummy_oid = self.cont.write_an_obj(
                        my_str, my_str_sz, dkey, akey, obj_cls="OC_SX")
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
            self.cont2.create(self.pool.pool.handle)
            self.d_log.debug("created 2nd container")

            self.d_log.debug("opening container 2")
            self.cont2.open()
            self.d_log.debug("opened container 2")

            self.d_log.debug("writing one more object, write expected to fail")
            self.cont2.write_an_obj(my_str, my_str_sz, dkey, akey,
                                    obj_cls="OC_SX")
            self.fail("wrote one more object after pool was completely filled,"
                      " this should never print")
        except DaosApiError as excep:
            if not (err in repr(excep) or err2 in repr(excep)):
                self.d_log.error("caught unexpected exception while "
                                 "writing object: {0}".format(repr(excep)))
                self.fail("caught unexpected exception while writing "
                          "object: {0}".format(repr(excep)))
            else:
                self.d_log.debug("correctly caught -1007 while attempting "
                                 "to write object in full pool")
