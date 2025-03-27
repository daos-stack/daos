# Function/variable naming conventions for the EFA Libfabric provider

This document describes the function/variable naming conventions for the EFA
Libfabric provider.

## Variable naming conventions

Global variables' names must start with `g_` prefix. For example,
`g_device_list` is a global variable that is an array of `struct efa_device`.

## Function naming conventions

A function perform an action to an object. Unlike human language that typically
puts actions in front of objects, function names put the object name before the
action. For example, a function that opens an EFA endpoint is named
`efa_ep_open`, not `open_efa_ep`.

Functions are organized by objects, which means functions which act on the same
object are put in same file. For example, functions `efa_ep_open()` and
`efa_ep_close()` are located in `efa_ep.c`.

Typical objects include `efa_ep`, `efa_cq`, `efa_device`.

Typical actions include:

- `construct` initializes data members of a struct, but does not allocate the
  memory for the input. Typically, a function named `xxx_construct()` should
  define its first parameter to be a pointer of type `struct xxx`.  For example,
  `efa_device_construct()`'s first parameter is a pointer of `struct efa_device
  *`. The function initializes the data members of an `efa_device` struct.
- `destruct` works in the opposite direction of `construct`. It releases
  resources associated with data members of a struct, but does not release the
  memory of the struct itself.
- `open` allocates an object and initializes its data members. Typically, a
  function named `xxx_open()` will define a parameter for a pointer to a pointer
  of type `struct xxx`. On success, the argument will be set to be pointing to a
  pointer of type `struct xxx`. For example, the 3rd argument `ep_fid` of
  function `efa_ep_open()` is of type `struct fid_ep **`.  On success, `ep_fid`
  will be pointing to a newly created `struct fid_ep` object.
- `close` works in the opposite direction of `open`. It releases all resources
  of an object, then free the memory of the object. Typically, a function named
  `xxx_close` defines a parameter for a pointer to `struct xxx`.  For example,
  `efa_ep_close()` takes a pointer named `ep` of type `struct ep_fid *` as
  input. It releases the resources of `ep`, then releases the memory pointed by
  ep.
- `alloc` has the same behavior as `open`. It allocates memory for an object,
  then initializes its data members. `open` is used for larger objects, like
  endpoint (ep) and completion queue (cq). `alloc` is used for smaller objects,
  like `txe` (TX entry) and `rxe` (RX entry).
- `release` works in the opposite direction of `alloc`, and is used on objects
  that `alloc` is used on.
- `initialize` is used to initialize global variables. For example,
  `efa_device_list_initialize` initializes the global variables `g_device_list`
  and `g_device_cnt`.
- `finalize` works in the opposite direction of `initialize`, which means it is
  used to release resources associated with global variables.
