# Native Object Interface

## Building against the DAOS library

To build an application or I/O middleware against the native DAOS API, include
the `daos.h` header file in your program and link with `-Ldaos`. Examples are
available under `src/tests`.

## DAOS API Reference

`libdaos` is written in C and uses Doxygen comments that are added to C header
files. The Doxygen documentation is available
[here](https://docs.daos.io/v2.6/doxygen/html/).

## Python Bindings

The pydaos.raw submodule provides access to DAOS API functionality via Ctypes
and was developed with an emphasis on test use cases. While the majority of unit
tests are written in C, higher-level tests are written primarily using the
Python API. Interfaces are provided for accessing DAOS management and DAOS API
functionality from Python. This higher level interface allows a faster
turnaround time on implementing test cases for DAOS.

#### Layout

The Python API is split into several files based on functionality:

* The Python object API:
  [daos_api.py](https://github.com/daos-stack/daos/tree/master/src/client/pydaos/raw/daos_api.py).
* The mapping of C structures to Python classes
  [daos_cref.py](https://github.com/daos-stack/daos/tree/master/src/client/pydaos/raw/daos_cref.py)

High-level abstraction classes exist to manipulate DAOS storage:
```python
class DaosPool(object)
class DaosContainer(object)
class DaosObj(object)
class IORequest(object)
```

`DaosPool` is a Python class representing a DAOS pool. All pool-related
functionality is exposed from this class. Operations such as creating/destroying
a pool, connecting to a pool, and adding a target to a storage pool are
supported.

`DaosContainer` is a Python class representing a DAOS container.
As with the `DaosPool` class, all container-related functionality is exposed
here. This class also exposes abstracted wrapper functions for the flow of
creating and committing an object to a DAOS container.

`DaosObj` is a Python class representing a DAOS object. Functionality such as
creating/deleting objects in a container, 'punching' objects (delete an object
from the specified transaction only), and object query.

`IORequest` is a Python class representing a read or write request against a
DAOS object.

Several classes exist for management purposes as well:
```python
class DaosContext(object)
class DaosLog
class DaosApiError(Exception)
```

`DaosContext` is a wrapper around the DAOS libraries. It is initialized with the
path where DAOS libraries can be found.

`DaosLog` exposes functionality to write messages to the DAOS client log.

`DaosApiError` is a custom exception class raised by the API internally in the
event of a failed DAOS action.

Most functions exposed in the DAOS C API support both synchronous and
asynchronous execution, and the Python API exposes this same functionality.
Each API takes an input event. `DaosEvent` is the Python representation of this
event. If the input event is `NULL`, the call is synchronous. If an event is
supplied, the function will return immediately after submitting API requests to
the underlying stack, and the user can poll and query the event for completion.

#### Ctypes

Ctypes is a built-in Python module for interfacing Python with existing
libraries written in C/C++. The Python API is built as an object-oriented
wrapper around the DAOS libraries utilizing ctypes.

Ctypes documentation can be found here <https://docs.python.org/3/library/ctypes.html>

The following demonstrates a simplified example of creating a Python wrapper
for the C function `daos_pool_tgt_exclude_out`, with each input parameter to the
C function being cast via ctypes. This also demonstrates struct representation via ctypes:

```C
// daos_exclude.c

#include <stdio.h>

int
daos_pool_tgt_exclude_out(const uuid_t uuid, const char *grp,
                          struct d_tgt_list *tgts, daos_event_t *ev);
```

All input parameters must be represented via ctypes. If a struct is required as
an input parameter, a corresponding Python class can be created. For struct `d_tgt_list`:

```c
struct d_tgt_list {
	d_rank_t	*tl_ranks;
	int32_t		*tl_tgts;
	uint32_t	tl_nr;
};
```
```python
class DTgtList(ctypes.Structure):
    _fields_ = [("tl_ranks", ctypes.POINTER(ctypes.c_uint32)),
                ("tl_tgts", ctypes.POINTER(ctypes.c_int32)),
                ("tl_nr", ctypes.c_uint32)]
```

The shared object containing `daos_pool_tgt_exclude_out` can then be imported
and the function called directly:

```python
# api.py

import ctypes
import uuid
import conversion # utility library to convert C <---> Python UUIDs

# init python variables
p_uuid = str(uuid.uuid4())
p_tgts = 2
p_ranks = DaosPool.__pylist_to_array([2])

# cast python variables via ctypes as necessary
c_uuid = str_to_c_uuid(p_uuid)
c_grp = ctypes.create_string_buffer(b"daos_group_name")
c_tgt_list = ctypes.POINTER(DTgtList(p_ranks, p_tgts, 2))) # again, DTgtList must be passed as pointer

# load the shared object
my_lib = ctypes.CDLL('/full/path/to/daos_exclude.so')

# now call it
my_lib.daos_pool_tgt_exclude_out(c_uuid, c_grp, c_tgt_list, None)
```

#### Error Handling

The API was designed using the EAFP (<b>E</b>asier to <b>A</b>sk
<b>F</b>orgiveness than get <b>P</b>ermission) idiom. A given function will
raise a custom exception on error state, `DaosApiError`.
A user of the API is expected to catch and handle this exception as needed:

```python
# catch and log
try:
    daos_some_action()
except DaosApiError as e:
    self.d_log.ERROR("My DAOS action encountered an error!")
```

#### Logging

The Python DAOS API exposes functionality to log messages to the DAOS client log.
Messages can be logged as `INFO`, `DEBUG`, `WARN`, or `ERR` log levels.
The DAOS log object must be initialized with the environmental context in which to run:

```python
from pydaos.raw import DaosLog

self.d_log = DaosLog(self.context)

self.d_log.INFO("FYI")
self.d_log.DEBUG("Debugging code")
self.d_log.WARNING("Be aware, may be issues")
self.d_log.ERROR("Something went very wrong")
```
## Go Bindings

API bindings for Go[^2] are also available.

[^1]: https://github.com/daos-stack/daos/blob/master/src/client/pydaos/raw/README.md

[^2]: https://godoc.org/github.com/daos-stack/go-daos/pkg/daos
