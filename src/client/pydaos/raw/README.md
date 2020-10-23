# Python Bindings for DAOS API

## Python API Usage in Tests

The following example demonstrates a simple use case for the Python API for DAOS, connecting to a pool,
creating a container within the pool, and inserting a single value:

```python

# initialize DAOS environmental context
with open('../../../.build_vars.json') as f:
    data = json.load(f)

context = DaosContext(data['PREFIX'] + '/lib/')
print("Initialized!")

# connect to a DAOS pool
pool = DaosPool(context)
pool.set_uuid_str(os.environ['DAOS_POOL_UUID'])
pool.set_group('daos_server')
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

## Changing the API

### Extending DAOS Python API

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
            'connect-pool'   : self.libdaos.daos_pool_connect,
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

### C API Modifications

If an existing function is modified, a corresponding update must be made to the Python API. For example, if the member `foo` of `my_struct` were to change from `int foo` to `int *foo`, the Python class representing `my_struct` must also be modified:

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

### C API Removal

If an existing C API is removed, the corresponding Python function must also be removed.
