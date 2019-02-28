# Python Bindings for DAOS API

## What is DAOS?

The <b>D</b>istributed <b>A</b>synchronous <b>O</b>bject <b>S</b>torage (DAOS) is an open-source software-defined object store designed from the ground up for massively distributed Non Volatile Memory (NVM). DAOS takes advantage of next generation NVM technology like Storage Class Memory (SCM) and NVM express (NVMe) while presenting a key-value storage interface and providing features such as transactional non-blocking I/O, advanced data protection with self healing on top of commodity hardware, end-to-end data integrity, fine grained data control and elastic storage to optimize performance and cost.

## Motivation for Python API

The Python API for DAOS is intended to be used for testing purposes. While the majority of unit testing is written in C, functional testing is written primarily using the Python API. Interfaces are provided for accessing DAOS management and DAOS API functionality from Python. This higher level interface allows a faster turnaround time on implementing test cases for DAOS.

<!--Extend this section - typical use cases, e.g. want to programmatically create a pool from a python test case.-->

## Architecture

### Layout
The Python API is split into several files based on functionality. As with the DAOS API, it can be broadly split into two categories, DAOS API and DAOS Management. The API is located in the source tree at [src/utils/py/daos_api.py](daos_api.py).

High-level abstraction classes exist to manipulate the tiers of DAOS storage:
```python
class DaosPool(object)
class DaosContainer(object)
class DaosObj(object)
class IORequest(object)
```

Several classes exist for management purposes as well:
```python
class DaosContext(object)
class DaosServer(object)
class DaosLog
class DaosApiError(Exception)
```

Internally, the Python API leverages a set of utilities within the src/utils/py directory:
* All necessary Python representations of DAOS C API structs are mirrored via ctypes in [src/utils/py/daos_cref.py](daos_cref.py)
* Utility functions for converting C UUIDs to Python UUIDs and vice versa can be found in [src/utils/py/conversion.py](conversion.py)

Additionally, a set of high-level abstractions are provided to test developers in `/src/tests/ftest/util/`. These include:
* [ServerUtils](../../tests/ftest/util/ServerUtils.py) provides a wrapper around DAOS server launch/shutdown functionality
* [AgentUtils](../../tests/ftest/util/AgentUtils.py) contains functionality to launch/shutdown the DAOS security agent
* [IorUtils](../../tests/ftest/util/IorUtils.py) is a high level driver for running IOR tests over DAOS

### Ctypes

The Python API is built as a pass-through to the DAOS C API utilizing a Python module called ctypes. Ctypes is a built-in module for interfacing Python with existing libraries written in C/C++. The Python API leverages ctypes to act as a middle ground between a traditional transparent shim and a Pythonic object-oriented wrapper around the C API.

### Error Handling

The API was designed using the EAFP (<b>E</b>asier to <b>A</b>sk <b>F</b>orgiveness than get <b>P</b>ermission) idiom. A given function will raise a custom exception on error state, `DaosApiError`. A user of the API is expected to catch and handle this exception as needed:

```python
# catch and log
try:
    daos_some_action()
except DaosApiError as e:
    self.d_log.ERROR("My DAOS action encountered an error!")
```






<!-- Extend this section:
basic c-types concepts?
how c-types concepts are applied in our api code?
python API object-oriented design?
python API location in source code tree and how split across files?
Also stop using "The Python API" as a sentence opener
-->

## Usage

### Initializing DAOS C API

In the Python API, DAOS shared objects are loaded within a Python object called a `DaosContext`. This describes the environmental context in which to run. A simplified version of this object is shown below:

```python
import ctypes

class DaosContext(object):
    """
    Provides environment and other info for a DAOS client.
    """
    def __init__(self, path):
        # init
        libdaos = ctypes.CDLL("/path/to/libdaos.so.0.0.2", mode=ctypes.DEFAULT_MODE)
        ctypes.CDLL("/path/to/libdaos_common.so", mode=ctypes.RTLD_GLOBAL)

        # test lib exposes functionality for tests to write to DAOS client log
        libtest = ctypes.CDLL("/path/to/libdaos_tests.so", mode=ctypes.DEFAULT_MODE)

        # call C API's init
        libdaos.daos_init()
```

### Python API Usage in Tests

The following code snippet is an example of how to create a DAOS pool using the DAOS Python API:

<!--Need some better intro sentence/paragraph here, this is rough-->

```python
import os
from daos_api import DaosContext, DaosLog

class MyTest(Test):
    def test_foo(self):
        with open('/path/to/.build_vars.json') as f:
            build_paths = json.load(f)

        # create the context
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.d_log = DaosLog(self.context)

        # create the Python pool object
        self.pool = DaosPool(self.Context)

        # set values for arguments to pass to pool create function
        createmode = 73             # create as RO
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = "daos_server" # group name
        createsize = 1000000000     # size of created pool

        # create the actual DAOS storage pool
        self.pool.create(createmode, createuid, creategid, createsize,
                         createsetid, None)
```

### Changing the API

#### Extending DAOS Python API

<!--Extend this section: Change in C API parameters or function name-->

Once a function has been added to the DAOS C API, it must be represented in the Python API. In the following example, the function table is extended to reference a new C API function `hello_world()`, and a corresponding Python function is created:

```c
void
hello_world(int *a, int b);
```

```python
class DaosContext(object):
    def __init__(self, path):

        # table defining relationship between Python and C function calls
        self.ftable = {
            'create-pool'    : self.libdaos.daos_pool_create,
            'hello-world'    : self.libdaos.daos_hello_world
        }

# return the specified function from function table
def get_function(self, function):
    return self.ftable[function]

# a corresponding hello_world Python API function is added
def hello_world(self):
    # paths relative to build directory to set up the environmental context
    with open('/path/to/.build_vars.json') as f:
        build_paths = json.load(f)
    self.context = DaosContext(build_paths['PREFIX'] + '/lib/')

    # retrieve new function from function table
    func = self.context.get_function('hello-world')

    # ensure arguments passed are of the correct type
    a = ctypes.byref(1)
    b = ctypes.c_int(2)

    # call it
    rc = func(a, b)
```

#### C API Modifications

If an existing C API is modified, there is currently no automated mechanism to update the corresponding Python API. Accordingly, users must be diligent to note if a test fails at run time and determine if the failure was due to a change in the C API. For example, a change of parameter `int foo` to function `hello_world(int foo)` to `uint foo` would require the respective Python API's `foo` field to be changed to be converted to `ctypes.uint`.

#### C API Removal

If an existing C API is removed, the corresponding Python function must also be removed.

### Logging

The Python DAOS API exposes functionality to log messages to the DAOS client log. Messages can be logged as `INFO`, `DEBUG`, `WARN`, or `ERR` log levels. A test must initialize the DAOS log object with the environmental context in which to run:

```python
from daos_api import DaosContext, DaosLog

with open('/path/to/.build_vars.json') as f:
    build_paths = json.load(f)

self.context = DaosContext(build_paths['PREFIX'] + '/lib/')\
self.d_log = DaosLog(self.context)

self.d_log.INFO("FYI")
self.d_log.DEBUG("Debugging code")
self.d_log.WARNING("Be aware, may be issues")
self.d_log.ERROR("Something went very wrong")
```

### Teardown

```python
# fini must be called to de-init when completed
libdaos.daos_fini()
```