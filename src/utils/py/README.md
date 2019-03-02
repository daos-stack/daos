# Python Bindings for DAOS API

## Motivation for Python API

The Python API for DAOS provides access to DAOS API functionality with an emphasis on test use cases. While the majority of unit tests are written in C, higher-level tests are written primarily using the Python API. Interfaces are provided for accessing DAOS management and DAOS API functionality from Python. This higher level interface allows a faster turnaround time on implementing test cases for DAOS.

## Architecture

### Layout

The Python API is split into several files based on functionality:

* The Python object API, located in the source tree at [src/utils/py/daos_api.py](daos_api.py).
* The mapping of C structures to Python classes, located in the source tree at [src/utils/py/daos_cref.py](daos_cref.py)

High-level abstraction classes exist to manipulate the tiers of DAOS storage:
```python
class DaosPool(object)
class DaosContainer(object)
class DaosObj(object)
class IORequest(object)
```

`DaosPool` is a Python class representing a DAOS pool. All pool-related functionality is exposed from this class. Operations such as creating/destroying a pool, connecting to a pool, and adding a target to a storage pool are supported.

`DaosContainer` is a Python class representing a DAOS container. As with the `DaosPool` class, all container-related functionality is exposed here. This class also exposes abstracted wrapper functions for the flow of creating and committing an object to a DAOS container.

`DaosObj` is a Python class representing a DAOS object. Functionality such as creating/deleting objects in a container, 'punching' objects (delete an object from the specified transaction only), and object query.

`IORequest` is a Python class representing a DAOS transaction. Functionality is exposed to create, delete, and modify DAOS transactions.

Several classes exist for management purposes as well:
```python
class DaosContext(object)
class DaosLog
class DaosApiError(Exception)
```

`DaosContext` is a wrapper around the DAOS libraries. It is initialized with the path where DAOS libraries can be found. It describes the environmental context in which to run.

`DaosLog` exposes functionality to write messages to the DAOS client log.

`DaosApiError` is a custom exception class raised by the API internally in the event of a failed DAOS action.

### Ctypes

Ctypes is a built-in Python module for interfacing Python with existing libraries written in C/C++. The Python API is built as an object-oriented wrapper around the DAOS libraries utilizing ctypes.

Ctypes documentation can be found here https://docs.python.org/3/library/ctypes.html

The following demonstrates calling a C function `hello_world` from Python, with each input parameter to the C function being cast via ctypes:

```C
// hello_world.c

#include <stdio.h>

void hello_world(int *a, int b);

void hello_world(int *a, int b) {
    printf("hello world\n");
}
```

```python
# hello_world.py

import ctypes

# correctly cast the parameters via ctypes to match C function's expected input
a = ctypes.byref(1)
b = ctypes.c_int(2)

# load the shared object
my_lib = ctypes.CDLL('/full/path/to/hello_world.so')

# now call it
my_lib.hello_world(a, b)
```

In the event the C API takes in a struct as a parameter, the struct will also need to be represented via ctypes in Python:

```C
// hello_world.c

#include <stdio.h>

void hello_world(int *a, int b);

typedef struct {
    int foo;
} my_struct;

void hello_world(int *a, my_struct b) {
    printf("hello world\n");
}
```

```python
# hello_world.py

import ctypes

class MyStruct(ctypes.Structure):
    _fields_ = [("foo", ctypes.c_int)]

# correctly cast the parameters via ctypes to match C function's expected input
a = ctypes.byref(1)
b = MyStruct(2)

# load the shared object
my_lib = ctypes.CDLL('/full/path/to/hello_world.so')

# now call it
my_lib.hello_world(a, b)
```

### Error Handling

The API was designed using the EAFP (<b>E</b>asier to <b>A</b>sk <b>F</b>orgiveness than get <b>P</b>ermission) idiom. A given function will raise a custom exception on error state, `DaosApiError`. A user of the API is expected to catch and handle this exception as needed:

```python
# catch and log
try:
    daos_some_action()
except DaosApiError as e:
    self.d_log.ERROR("My DAOS action encountered an error!")
```

## Usage

### Python API Usage in Tests

The following example demonstrates a simple use case for the Python API for DAOS, creating and connecting to a pool, creating a container within the pool, and inserting a single value:

```python

# initialize DAOS environmental context
with open('../../../.build_vars.json') as f:
    data = json.load(f)

context = DaosContext(data['PREFIX'] + '/lib/')
print("Initialized!")

# create a DAOS pool
pool = DaosPool(context)
pool.create(448, os.getuid(), os.getgid(), 1024 * 1024 * 1024, b'daos_server')
print("Pool UUID is {0}".format(pool.get_uuid_str()))

# connect to it
pool.connect(1 << 1)

# query the pool
pool_info = pool.pool_query()
print("Pool has {0} storage targets".format(pool_info.pi_ntargets))
print("Pool created with {0} permissions".format(pool_info.pi_mode))

# create a container in the pool and open it
container = DaosContainer(context)
container.create(pool.handle)
container.open()

# prep an object to write to the container
thedata = "data to write to this object"
size = 28
dkey = "this is the dkey"
akey = "this is the akey"

# write it
obj, tx = container.write_an_obj(thedata, size, dkey, akey, None, 5)
```

### Changing the API

#### Extending DAOS Python API

Once a function has been added to the DAOS C API, it must be represented in the Python API. In the following example, the function table is extended to reference a new C API function `hello_world()`, and a corresponding Python function is created.

1. A C function is added to the DAOS API:

```c
void
hello_world(int *a, int b);
```

2. The C function is added to the function table in the `DaosContext` class in `daos_api.py`:

```python
class DaosContext(object):
    def __init__(self, path):

        # table defining relationship between Python and C function calls
        self.ftable = {
            'create-pool'    : self.libdaos.daos_pool_create,
            'hello-world'    : self.libdaos.daos_hello_world # this is the new function
        }
```

3. A corresponding Python function is added in `daos_api.py`. Consideration must be given to whether the added function is an operation on an existing Python class or if a new class must be created:

```python
# a corresponding hello_world Python API function is added
def hello_world(self):

    # retrieve new function from function table
    func = self.context.get_function('hello-world')

    # ensure arguments passed are of the correct type
    a = ctypes.byref(1)
    b = ctypes.c_int(2)

    # call it
    rc = func(a, b)
    if rc != 0:
        raise DaosApiError("function hello_world encountered an error!")
```

#### C API Modifications

If an existing C API is modified, a corresponding update must be made to the Python API. For example, if the member `foo` of `my_struct` were to change from `int foo` to `int *foo`, the Python class representing `my_struct` must also be modified:

```c
typedef struct {
    int *foo;
} my_struct;
```

```python
class MyStruct(ctypes.Structure):
    # field 'foo' updated to correctly cast as pointer vs. int
    _fields_ = [("foo", ctypes.POINTER(ctypes.c_int))]
```

Similarly, if existing APIs add or remove an input parameter, the relevant parameters must be modified in the respective Python APIs.

#### C API Removal

If an existing C API is removed, the corresponding Python function must also be removed.

### Logging

The Python DAOS API exposes functionality to log messages to the DAOS client log. Messages can be logged as `INFO`, `DEBUG`, `WARN`, or `ERR` log levels. The DAOS log object must be initialized with the environmental context in which to run:

```python
from daos_api import DaosLog

self.d_log = DaosLog(self.context)

self.d_log.INFO("FYI")
self.d_log.DEBUG("Debugging code")
self.d_log.WARNING("Be aware, may be issues")
self.d_log.ERROR("Something went very wrong")
```