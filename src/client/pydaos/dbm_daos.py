# (C) Copyright  2019 Intel Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
# The Government's rights to use, modify, reproduce, release, perform, display,
# or disclose this software are subject to the terms of the Apache License as
# provided in Contract No. B609815.
# Any reproduction of computer software, computer software documentation, or
# portions thereof marked with this legend must also reproduce the markings.
"""
pydaos.dbm module for emulating dbm.gnu
"""

import pydaos

class error(Exception):
    """Exception raised by daosbm() module"""

class daosdm():
    """dbm.gnu() like interface to daos"""

    def __init__(self, kv):
        self._dc = pydaos.DaosClient()
        self._kv = kv
        self._prevkey = None
        self._iter_obj = None

    def __getitem__(self, key):
        return self._kv[key]

    def firstkey(self):
        """Return the first key in the db"""

        iter_obj = self._kv.__iter__()
        try:
            key = iter_obj.__next__()
            self._prevkey = key
            self._iter_obj = iter_obj
            return key
        except StopIteration:
            return None

    def nextkey(self, key):
        """Return the next key in the db"""

        # Be safe and check for None first.
        if key is None:
            return None

        # If the iterator is carrying on where the
        # previous left off then just use it.
        if key == self._prevkey:
            try:
                newkey = self._iter_obj.__next__()
                self._prevkey = newkey
                return newkey
            except StopIteration:
                return None
        # If the iterator is not using the previously
        # returned key then search for the key, and start
        # from there.

        iter_obj = self._kv.__iter__()
        try:
            newkey = None
            while newkey != key:
                newkey = iter_obj.__next__()
            # Now that we've found the key, return the next
            # one.
            newkey = iter_obj.__next__()
            self._prevkey = newkey
            self._iter_obj = iter_obj
            return newkey
        except StopIteration:
            return None

    def reorganize(self):
        """For compatibility with dbm.gnu"""
        pass

    def sync(self):
        """For compatibility with dbm.gnu"""
        pass

    def close(self):
        """Close the key-value store in the container"""
        self._kv = None

class daosdm_rw(daosdm):
    """Extends daosdm() with write capability"""

    def __setitem__(self, key, value):
        self._kv[key] = value

    def __delitem__(self, key):
        self._kv[key] = None

def open(pool_uuid, cont_uuid, flag):
    """Open a container and return a daosdm() object"""

    if flag not in ['r', 'w']:
        raise error

    container = pydaos.Cont(pool_uuid, cont_uuid)

    kvstore = container.rootkv()

# pylint: disable=redefined-variable-type
    if flag == 'w':
        db = daosdm_rw(kvstore)
    else:
        db = daosdm(kvstore)
# pylint: enable=redefined-variable-type
    return db
