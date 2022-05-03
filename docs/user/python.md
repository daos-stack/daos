# PyDAOS

A python module called [PyDAOS](https://github.com/daos-stack/daos/blob/release/2.2/src/client/pydaos)
provides the DAOS API to python users. It aims at providing a pythonic interface
to the DAOS objects by exposing them via native python data structures.
This section focuses on the main PyDAOS interface that comes with its own
container type and layout. It does not cover the python bindings for the native
DAOS API, which is available via the [PyDAOS.raw](#Native_Programming_Interface)
submodule.

## Design

PyDAOS is a python module primarily written in C. It exposes DAOS key-value
store objects as a python dictionary. Other data structures (e.g., Array
compatible with NumPy) are under consideration.
Python objects allocated by PyDAOS are:

- **persistent** and identified by a string name. The namespace is shared
  by all the objects and implemented by a root key-value store storing the
  association between names and objects.
- immediately **visible** upon creation to any process running on the same
  or a different node.
- not consuming any significant amount of memory. Objects have a **very low
  memory footprint** since the actual content is stored remotely.  This allows
  manipulation of massive datasets that are way bigger than the amount of
  memory available on the node.

## Python Container

To create a python container in a pool labeled tank:

```bash
$ daos cont create tank --label neo --type PYTHON
  Container UUID : 3ee904b3-8868-46ed-96c7-ef608093732c
  Container Label: neo
  Container Type : PYTHON

Successfully created container 3ee904b3-8868-46ed-96c7-ef608093732c
```

One can then connect to the container by passing the pool and container
labels to the DCont constructor:

```bash
>>> import pydaos
>>> dcont = pydaos.DCont("tank", "neo")
>>> print(dcont)
tank/neo
```

!!! note
    PyDAOS has its own container layout and will thus refuse to access
    a container that is not of type "PYTHON"

## DAOS Dictionaries

The first data structure exported by the PyDAOS module is the DAOS
Dictionary (DDict) that aims at mimicking the python dict interface. Leveraging
Mutable mapping and UserDict have been considered during design, but eventually
ruled out for performance reasons. The DDict class is built over DAOS key-value
stores and supports all the methods of the regular python dictionary class.
One limitation is that only strings and bytes can be stored.

A new DDict object can be allocated by calling the dict() method on the parent
python container.

```bash
>>> dd = dcont.dict("stadium", {"Barcelona" : "Camp Nou", "London" : "Wembley"})
>>> print(dd)
stadium
>>> print(len(dd))
2
```

This creates a new persistent object named "stadium" and initializes it with two
key-value pairs.

Once the dictionary created, it is persistent and cannot be overridden:

```bash
>>> dd1 = dcont.dict("stadium")
Traceback (most recent call last):
  File "<stdin>", line 1, in <module>
  File "/home/jlombard/src/new/daos/install/lib64/python3.6/site-packages/pydaos/pydaos_core.py", line 116, in dict
    raise PyDError("failed to create DAOS dict", ret)
pydaos.PyDError: failed to create DAOS dict: DER_EXIST
```

!!! note
    For each method, a PyDError exception is raised with a proper DAOS error code
    (in string format) if the operation cannot be completed.

To retrieve an existing dictionary, use the get() method:

```bash
>>> dd1 = dcont.get("stadium")
```

New records can be inserted one at a time via put operation. Existing
records can be fetched via the get() operation. Similar to the Python dictionary,
direct assignment is also supported.

```bash
>>> dd["Milano"] = "San Siro"
>>> dd["Rio"] = "Maracanã"
>>> print(dd["Milano"])
b'San Siro'
>>> print(len(dd))
4
```

Key-value pairs can also be inserted/looked up in bulk via the bput()/bget()
methods, taking a python dict as an input. The bulk operations are issued in
parallel (up to 16 operations in flight) to maximize the operation rate.

```bash
>>> dd.bput({"Madrid" : "Santiago-Bernabéu", "Manchester" : "Old Trafford"})
>>> print(len(dd))
6
>>> print(dd.bget({"Madrid" : None, "Manchester" : None}))
{'Madrid': b'Santiago-Bernabéu', 'Manchester': b'Old Trafford'}
```

Key-value pairs are deleted via the put/bput operations by setting the value
to either None or the empty string. Once deleted, the key won't be reported
during iteration. It also supports the del operation via the del() and pop()
methods.

```bash
>>> del dd["Madrid"]
>>> dd.pop("Rio")
>>> print(len(dd))
4
```

The key space can be worked through via python iterators.

```bash
>>> for key in dd: print(key, dd[key])
...
Manchester b'Old Trafford'
Barcelona b'Camp Nou'
Milano b'San Siro'
London b'Wembley'
```

The content of a DAOS dictionary can be exported to a regular python dictionary
via the dump() method.

```bash
>>> d = dd.dump()
>>> print(d)
{'Manchester': b'Old Trafford', 'Barcelona': b'Camp Nou', 'Milano': b'San Siro', 'London': b'Wembley'}
```

!!! warning
    When using the dump() method for a large DAOS dictionary requires care.

The resulting python dictionary will be reported as equivalent to the original.
DAOS dictionary.

```bash
>>> d == dd
True
```

And will be reported as different as both objects diverge.

```bash
>>> dd["Rio"] = "Maracanã"
>>> d == dd
False
```

One can also directly test whether a key is in the dictionary.

```bash
>>> "Rio" in dd
True
>>> "Paris" in dd
False
```

## Arrays

Class representing of DAOS array leveraging the NumPy’s dispatch mechanism.
See [https://numpy.org/doc/stable/user/basics.dispatch.html](https://numpy.org/doc/stable/user/basics.dispatch.html) for more info.
Work in progress
