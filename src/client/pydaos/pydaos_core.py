# (C) Copyright 2019-2022 Intel Corporation.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
"""
PyDAOS Module allowing global access to the DAOS containers and objects.
"""

import enum

# pylint: disable=relative-beyond-top-level
from . import pydaos_shim
# pylint: enable=relative-beyond-top-level

from . import DAOS_MAGIC
from . import PyDError
from . import DaosClient

# Import Object class as an enumeration
ObjClassID = enum.Enum(
    "Enumeration of the DAOS object classes (OC).",
    {key: value for key, value in list(pydaos_shim.__dict__.items())
     if key.startswith("OC_")})


class DObjNotFound(Exception):
    """Raised by get if name associated with DAOS object not found """

    def __init__(self, name):
        self.name = name
        super().__init__(self)

    def __str__(self):
        return "Failed to open '{}'".format(self.name)


class DCont():
    """
    Class representing of DAOS python container
    Can be identified via a path or a combination of pool label and container
    label (alternatively, UUID strings are supported too). DAOS pool and
    container are opened during the __init__ phase and closed on __del__.

    Attributes
    ----------
    pool : string
        Pool label or UUID string
    cont : string
        Container label or UUID string
    path : string
        Path for container representation in unified namespace

    Methods
    -------
    get(name):
        Return DAOS object (darray or ddict) associated with name.
        If not found, the DObjNotFound Exception is raised.

    dict(name, kwargs):
        Create new DDict object.

    array(name, kwargs):
        Create new DArray object.
    """

    def __init__(self, pool=None, cont=None, path=None):
        self._dc = DaosClient()
        self._hdl = None
        if path is None and (pool is None or cont is None):
            raise PyDError("invalid pool or container UUID",
                           -pydaos_shim.DER_INVAL)
        if path is not None:
            self.pool = None
            self.cont = None
            (ret, hdl) = pydaos_shim.cont_open_by_path(DAOS_MAGIC, path, 0)
        else:
            self.pool = pool
            self.cont = cont
            (ret, hdl) = pydaos_shim.cont_open(DAOS_MAGIC, pool, cont, 0)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to access container", ret)
        self._hdl = hdl

    def __del__(self):
        if not self._hdl:
            return
        ret = pydaos_shim.cont_close(DAOS_MAGIC, self._hdl)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to close container", ret)

    def get(self, name):
        """ Look up DAOS object associated with name """

        (ret, hi, lo, otype) = pydaos_shim.cont_get(DAOS_MAGIC, self._hdl, name)
        if ret == -pydaos_shim.DER_NONEXIST:
            raise DObjNotFound(name)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to look up name", ret)

        if otype == pydaos_shim.PYDAOS_DICT:
            return DDict(name, self._hdl, hi, lo, self)
        if otype == pydaos_shim.PYDAOS_ARRAY:
            return DArray(name, self._hdl, hi, lo, self)

        raise DObjNotFound(name)

    def __getitem__(self, name):
        return self.get(name)

    def dict(self, name, v: dict = None):
        """ Create new DDict object """

        # Insert name into root kv and get back an object ID
        (ret, hi, lo) = pydaos_shim.cont_newobj(DAOS_MAGIC, self._hdl, name,
                                                pydaos_shim.PYDAOS_DICT)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to create DAOS dict", ret)

        # Instantiate the DDict() object
        dd = DDict(name, self._hdl, hi, lo, self)

        # Insert any records passed in kwargs
        dd.bput(v)

        return dd

    def array(self, name, v: list = None):
        # pylint: disable=unused-argument
        """ Create new DArray object """

        # Insert name into root kv and get back an object ID
        (ret, hi, lo) = pydaos_shim.cont_newobj(DAOS_MAGIC, self._hdl, name,
                                                pydaos_shim.PYDAOS_ARRAY)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to create DAOS array", ret)

        # Instantiate the DArray() object
        da = DArray(name, self._hdl, hi, lo, self)

        # Should eventually populate array with data in input list

        return da

    def __str__(self):
        return '{}/{}'.format(self.pool, self.cont)

    def __repr__(self):
        return 'daos://{}/{}'.format(self.pool, self.cont)


class _DObj():
    # pylint: disable=no-member

    def __init__(self, name, hdl, hi, lo, cont):
        self._dc = DaosClient()
        self.hi = hi
        self.lo = lo
        self.name = name
        # Set self.oh to None here so it's defined in __del__ if there's
        # a problem with the _open() call.
        self.oh = None
        # keep container around until all objects are gone
        self.cont = cont
        # Open the object
        self._open(hdl)

    def __del__(self):
        if self.oh is None:
            return
        # close the object
        self._close()

    def __str__(self):
        return self.name

    def __repr__(self):
        return "[" + hex(self.hi) + ":" + hex(self.lo) + "]"

# pylint: disable=too-few-public-methods


class DDictIter():

    """ Iterator class for DDict """

    def __init__(self, ddict):
        self._dc = DaosClient()
        self._entries = []
        self._nr = 256
        self._size = 4096  # optimized for 16-char strings
        self._anchor = None
        self._done = False
        self._kv = ddict

    def next(self):
        """for python 2 compatibility"""
        return self.__next__()

    def __next__(self):
        if len(self._entries) != 0:
            return self._entries.pop()
        if self._done:
            raise StopIteration()

        # read more entries
        (ret, nr, sz, anchor) = pydaos_shim.kv_iter(DAOS_MAGIC, self._kv.oh,
                                                    self._entries, self._nr,
                                                    self._size, self._anchor)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to enumerate Dictionary", ret)

        # save param for next iterations, those have been adjusted already by
        # the shim layer
        self._anchor = anchor
        self._nr = nr
        self._size = sz
        if self._anchor is None:
            # no more entries to consume
            self._done = True

        if len(self._entries) != 0:
            return self._entries.pop()
        raise StopIteration()
# pylint: enable=too-few-public-methods


class DDict(_DObj):
    """
    Class representing of DAOS dictionary (i.e. key-value store object).
    Only strings are supported for both the key and value for now.
    Key-value pair can be inserted/looked up once at a time (see put/get) or
    in bulk (see bput/bget) taking a python dict as an input. The bulk
    operations are issued in parallel (up to 16 operations in flight) to
    maximize the operation rate.
    Key-value pair are deleted via the put/bput operations by setting the value
    to either None or the empty string. Once deleted, the key won't be reported
    during iteration.
    The DAOS dictionaries behave like python dictionaries and support:
    - 'dd[key]' which invokes 'ddict.get(key)'
    - 'dd[key] = val' which invokes 'dd.put(key, val)'
    - 'for key in dd:' allows to walk through the key space via the support of
      python iterators
    - 'if key is in dd:' allows to test whether a give key is present in the
      DAOS dictionary.
    - 'len(dd)' returns the number of key-value pairs
    - 'bool(dd)' reports 'False' if there is no key-value pairs in the DAOS
      dictionary and 'True' otherwise.

    Python iterators are supported, which means that "for key in dd:" will
    allow you to walk through the key space.
    For each method, a PyDError exception is raised with proper DAOS error code
    (in string format) if the operation cannot be completed.

    Methods
    -------
    get(key)
        Retrieve value associated with the key.
        If found, the string value is returned, None is returned otherwise.
    put(key, val)
        Update/insert key-value pair. Both parameters should be strings.
    bget(ddict)
        Bulk get value for all the keys of the input python dictionary.
        Get operations are issued in parallel over the network.
        The existing value in ddict is overwritten with the value retrieved from
        DAOS. If the key isn't found, the value is set to None.
    bput(ddict)
        Bulk put all the key-value pairs of the input python dictionary.
        Put operations are issued in parallel over the network.
        If the value is set to None or an empty string, the key is deleted from
        the DAOS dictionary.
    dump()
        Fetch all the key-value pairs and return them in a python dictionary.
    """

    # Size of buffer to use for reads.  If the object value is bigger than this
    # then it'll require two round trips rather than one.
    value_size = 1024*1024

    def _open(self, hdl):
        (ret, oh) = pydaos_shim.kv_open(DAOS_MAGIC, hdl, self.hi, self.lo, 0)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to open object", ret)
        self.oh = oh

    def _close(self):
        ret = pydaos_shim.kv_close(DAOS_MAGIC, self.oh)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to close object", ret)

    def get(self, key):
        """Retrieve value associated with the key."""

        d = {key: None}
        self.bget(d)
        if d[key] is None:
            raise KeyError(key)
        return d[key]

    def __getitem__(self, key):
        return self.get(key)

    def put(self, key, val):
        """Update/insert key-value pair. Both parameters should be strings."""
        d = {key: val}
        self.bput(d)

    def __setitem__(self, key, val):
        self.put(key, val)

    def __delitem__(self, key):
        self.put(key, None)

    def pop(self, key):
        """Remove key from the dictionary."""
        self.put(key, None)

    def bget(self, d, value_size=None):
        """Bulk get value for all the keys of the input python dictionary."""
        if d is None:
            return d
        if value_size is None:
            value_size = self.value_size
        ret = pydaos_shim.kv_get(DAOS_MAGIC, self.oh, d, value_size)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to retrieve KV value", ret)
        return d

    def bput(self, d):
        """Bulk put all the key-value pairs of the input python dictionary."""
        if d is None:
            return
        ret = pydaos_shim.kv_put(DAOS_MAGIC, self.oh, d)
        if ret != pydaos_shim.DER_SUCCESS:
            raise PyDError("failed to store KV value", ret)

    def dump(self):
        """Fetch all the key-value pairs, return them in a python dictionary."""
        # leverage python iterator, see __iter__/__next__ below
        # could be optimized over the C library in the future
        d = {}
        for key in self:
            d[key] = None
        self.bget(d)
        return d

    def __len__(self):
        # Not efficient for now. Fetch all keys and count them.
        i = 0
        for _ in self:
            i += 1
        return i

    def __bool__(self):
        for _ in self:
            return True
        return False

    def __contains__(self, key):
        try:
            self.get(key)
            return True
        except KeyError:
            return False

    def __eq__(self, other):
        # Not efficient for now. Could use the bulk operation.
        if len(other) != len(self):
            return False
        try:
            for key in other:
                if not self[key] == other[key]:
                    return False
        except KeyError:
            return False

        return True

    def __ne__(self, other):
        return not self.__eq__(other)

    def __iter__(self):
        return DDictIter(self)

# pylint: disable=too-few-public-methods


class DArray(_DObj):
    """
    Class representing of DAOS array leveraging the numpy's dispatch mechanism.
    See https://numpy.org/doc/stable/user/basics.dispatch.html for more info.
    Work in progress.
    """

    def _open(self, hdl):
        raise NotImplementedError

    def _close(self):
        raise NotImplementedError

    def ___array_ufunc__(self, ufunc, method, *inputs, **kwargs):
        raise NotImplementedError

    def __array_function__(self, func, types, args, kwargs):
        raise NotImplementedError
