---
layout: page
title: fi_av(3)
tagline: Libfabric Programmer's Manual
---
{% include JB/setup %}

# NAME

fi_av \- Address vector operations

fi_av_open / fi_close
: Open or close an address vector

fi_av_bind
: Associate an address vector with an event queue.

fi_av_insert / fi_av_insertsvc / fi_av_remove
: Insert/remove an address into/from the address vector.

fi_av_lookup
: Retrieve an address stored in the address vector.

fi_av_straddr
: Convert an address into a printable string.

fi_av_insert_auth_key
: Insert an authorization key into the address vector.

fi_av_lookup_auth_key
: Retrieve an authorization key stored in the address vector.

fi_av_set_user_id
: Set the user-defined fi_addr_t for an inserted fi_addr_t.

# SYNOPSIS

```c
#include <rdma/fi_domain.h>

int fi_av_open(struct fid_domain *domain, struct fi_av_attr *attr,
    struct fid_av **av, void *context);

int fi_close(struct fid *av);

int fi_av_bind(struct fid_av *av, struct fid *eq, uint64_t flags);

int fi_av_insert(struct fid_av *av, void *addr, size_t count,
    fi_addr_t *fi_addr, uint64_t flags, void *context);

int fi_av_insertsvc(struct fid_av *av, const char *node,
    const char *service, fi_addr_t *fi_addr, uint64_t flags,
    void *context);

int fi_av_insertsym(struct fid_av *av, const char *node,
    size_t nodecnt, const char *service, size_t svccnt,
    fi_addr_t *fi_addr, uint64_t flags, void *context);

int fi_av_remove(struct fid_av *av, fi_addr_t *fi_addr, size_t count,
    uint64_t flags);

int fi_av_lookup(struct fid_av *av, fi_addr_t fi_addr,
    void *addr, size_t *addrlen);

fi_addr_t fi_rx_addr(fi_addr_t fi_addr, int rx_index,
	  int rx_ctx_bits);

const char * fi_av_straddr(struct fid_av *av, const void *addr,
      char *buf, size_t *len);

int fi_av_insert_auth_key(struct fid_av *av, const void *auth_key,
      size_t auth_key_size, fi_addr_t *fi_addr, uint64_t flags);

int fi_av_lookup_auth_key(struct fid_av *av, fi_addr_t addr,
      void *auth_key, size_t *auth_key_size);

int fi_av_set_user_id(struct fid_av *av, fi_addr_t fi_addr,
      fi_addr_t user_id, uint64_t flags);
```

# ARGUMENTS

*domain*
: Resource domain

*av*
: Address vector

*eq*
: Event queue

*attr*
: Address vector attributes

*context*
: User specified context associated with the address vector or insert
  operation.

*addr*
: Buffer containing one or more addresses to insert into address vector.

*addrlen*
: On input, specifies size of addr buffer.  On output, stores number
  of bytes written to addr buffer.

*fi_addr*
: For insert, a reference to an array where returned fabric addresses
  will be written.  For remove, one or more fabric addresses to remove.
  If FI_AV_USER_ID is requested, also used as input into insert calls
  to assign the user ID with the added address.

*count*
: Number of addresses to insert/remove from an AV.

*flags*
: Additional flags to apply to the operation.

*auth_key*
: Buffer containing authorization key to be inserted into the address
  vector.

*auth_key_size*
: On input, specifies size of auth_key buffer.  On output, stores number
  of bytes written to auth_key buffer.

*user_id*
: For address vectors configured with FI_AV_USER_ID, this defines the
  user ID to be associated with a specific fi_addr_t.

# DESCRIPTION

Address vectors are used to map higher-level addresses, which may be
more natural for an application to use, into fabric specific
addresses.  For example, an endpoint may be associated with a
struct sockaddr_in address, indicating the endpoint is reachable using
a TCP port number over an IPv4 address.  This may hold even if the
endpoint communicates using a proprietary network protocol.  The
purpose of the AV is to associate a higher-level address with
a simpler, more efficient value that can be used by the libfabric API in a
fabric agnostic way.  The mapped address is of type fi_addr_t and is
returned through an AV insertion call.  The fi_addr_t is designed such
that it may be a simple index into an array, a pointer to a structure,
or a compact network address that may be placed directly into protocol
headers.

The process of mapping an address is fabric and provider specific,
but may involve lengthy address resolution and fabric management
protocols.  AV operations are synchronous by default, but may be set
to operate asynchronously by specifying the FI_EVENT flag to
`fi_av_open`.  When requesting asynchronous operation, the application
must first bind an event queue to the AV before inserting addresses.  See
the NOTES section for AV restrictions on duplicate addresses.

## fi_av_open

fi_av_open allocates or opens an address vector.  The properties and
behavior of the address vector are defined by `struct fi_av_attr`.

```c
struct fi_av_attr {
	enum fi_av_type  type;        /* type of AV */
	int              rx_ctx_bits; /* address bits to identify rx ctx */
	size_t           count;       /* # entries for AV */
	size_t           ep_per_node; /* # endpoints per fabric address */
	const char       *name;       /* system name of AV */
	void             *map_addr;   /* base mmap address */
	uint64_t         flags;       /* operation flags */
};
```

*type*
: An AV type corresponds to a conceptual implementation of an address
  vector.  The type specifies how an application views data stored in
  the AV, including how it may be accessed.  Valid values are:

- *FI_AV_MAP*
: Addresses which are inserted into an AV are mapped to a native
  fabric address for use by the application.  The use of FI_AV_MAP
  requires that an application store the returned fi_addr_t value
  that is associated with each inserted address.  The advantage of
  using FI_AV_MAP is that the returned fi_addr_t value may contain
  encoded address data, which is immediately available when
  processing data transfer requests.  This can eliminate or reduce
  the number of memory lookups needed when initiating a transfer.
  The disadvantage of FI_AV_MAP is the increase in memory usage
  needed to store the returned addresses.  Addresses are stored in
  the AV using a provider specific mechanism, including, but not
  limited to a tree, hash table, or maintained on the heap.

- *FI_AV_TABLE*
: Addresses which are inserted into an AV of type FI_AV_TABLE are
  accessible using a simple index.  Conceptually, the AV may be
  treated as an array of addresses, though the provider may implement
  the AV using a variety of mechanisms.  When FI_AV_TABLE is used, the
  returned fi_addr_t is an index, with the index for an inserted
  address the same as its insertion order into the table.  The index
  of the first address inserted into an FI_AV_TABLE will be 0, and
  successive insertions will be given sequential indices.  Sequential
  indices will be assigned across insertion calls on the same AV.

- *FI_AV_UNSPEC*
: Provider will choose its preferred AV type. The AV type used will
  be returned through the type field in fi_av_attr.

*Receive Context Bits (rx_ctx_bits)*
: The receive context bits field is only for use with scalable
  endpoints.  It indicates the number of bits reserved in a returned
  fi_addr_t, which will be used to identify a specific target receive
  context.  See fi_rx_addr() and fi_endpoint(3) for additional details
  on receive contexts.  The requested number of bits should be
  selected such that 2 ^ rx_ctx_bits >= rx_ctx_cnt for the endpoint.

*count*
: Indicates the expected number of addresses that will be inserted
  into the AV.  The provider uses this to optimize resource
  allocations.

*ep_per_node*
: This field indicates the number of endpoints that will be associated
  with a specific fabric, or network, address.  If the number of
  endpoints per node is unknown, this value should be set to 0.  The
  provider uses this value to optimize resource allocations.  For
  example, distributed, parallel applications may set this to the
  number of processes allocated per node, times the number of
  endpoints each process will open.

*name*
: An optional system name associated with the address vector to create
  or open.  Address vectors may be shared across multiple processes
  which access the same named domain on the same node.  The name field
  allows the underlying provider to identify a shared AV.

  If the name field is non-NULL and the AV is not opened for read-only
  access, a named AV will be created, if it does not already exist.

*map_addr*
: The map_addr determines the base fi_addr_t address that a provider
  should use when sharing an AV of type FI_AV_MAP between processes.
  Processes that provide the same value for map_addr to a shared AV
  may use the same fi_addr_t values returned from an fi_av_insert call.

  The map_addr may be used by the provider to mmap memory allocated
  for a shared AV between processes; however, the provider is not
  required to use the map_addr in this fashion.  The only requirement
  is that an fi_addr_t returned as part of an fi_av_insert call on one
  process is usable on another process which opens an AV of the same
  name at the same map_addr value.  The relationship between the
  map_addr and any returned fi_addr_t is not defined.

  If name is non-NULL and map_addr is 0, then the map_addr used by the
  provider will be returned through the attribute structure.  The
  map_addr field is ignored if name is NULL.

*flags*
: The following flags may be used when opening an AV.

- *FI_EVENT*
: When the flag FI_EVENT is specified, all insert operations on this
  AV will occur asynchronously.  There will be one EQ error entry
  generated for each failed address insertion, followed by one
  non-error event indicating that the insertion operation has
  completed.  There will always be one non-error completion event for
  each insert operation, even if all addresses fail.  The context
  field in all completions will be the context specified to the insert
  call, and the data field in the final completion entry will report
  the number of addresses successfully inserted.
  If an error occurs during the asynchronous insertion, an error
  completion entry is returned (see [`fi_eq`(3)](fi_eq.3.html) for a
  discussion of the fi_eq_err_entry error completion struct).  The
  context field of the error completion will be the context that was
  specified in the insert call; the data field will contain the index
  of the failed address.  There will be one error completion returned
  for each address that fails to insert into the AV.

  If an AV is opened with FI_EVENT, any insertions attempted before an
  EQ is bound to the AV will fail with -FI_ENOEQ.

  Error completions for failed insertions will contain the index of
  the failed address in the index field of the error completion entry.

  Note that the order of delivery of insert completions may not match
  the order in which the calls to fi_av_insert were made.  The only
  guarantee is that all error completions for a given call to
  fi_av_insert will precede the single associated non-error
  completion.

- *FI_READ*
: Opens an AV for read-only access.  An AV opened for read-only access
  must be named (name attribute specified), and the AV must exist.

- *FI_SYMMETRIC*
: Indicates that each node will be associated with the same number of
  endpoints, the same transport addresses will be allocated on each
  node, and the transport addresses will be sequential.  This feature
  targets distributed applications on large fabrics and allows for
  highly-optimized storage of remote endpoint addressing.

- *FI_AV_USER_ID*
: Indicates that the user will be associating user-defined IDs with
  a address vector via fi_av_set_user_id. If the domain has been configured
  with FI_AV_AUTH_KEY or the user requires FI_AV_USER_ID support, using the
  FI_AV_USER_ID flag per fi_av_insert / fi_av_insertsvc / fi_av_remove is
  not supported. fi_av_set_user_id must be used.

## fi_close

The fi_close call is used to release all resources associated with an
address vector.  Note that any events queued on an event queue referencing
the AV are left untouched.  It is recommended that callers retrieve all
events associated with the AV before closing it.

When closing the address vector, there must be no opened endpoints associated
with the AV.  If resources are still associated with the AV when attempting to
close, the call will return -FI_EBUSY.

## fi_av_bind

Associates an event queue with the AV.  If an AV has been opened with
`FI_EVENT`, then an event queue must be bound to the AV before any
insertion calls are attempted.  Any calls to insert addresses before
an event queue has been bound will fail with `-FI_ENOEQ`.  Flags are
reserved for future use and must be 0.

## fi_av_insert

The fi_av_insert call inserts zero or more addresses into an AV.  The
number of addresses is specified through the count parameter.  The
addr parameter references an array of addresses to insert into the AV.
Addresses inserted into an address vector must be in the same format
as specified in the addr_format field of the fi_info struct provided when
opening the corresponding domain. When using the `FI_ADDR_STR` format,
the `addr` parameter should reference an array of strings (char \*\*).

For AV's of type FI_AV_MAP, once inserted addresses have been mapped,
the mapped values are written into the buffer referenced by fi_addr.
The fi_addr buffer must remain valid until the AV insertion has
completed and an event has been generated to an associated event
queue.  The value of the returned fi_addr should be considered opaque
by the application for AVs of type FI_AV_MAP.  The returned value may
point to an internal structure or a provider specific encoding of
low-level addressing data, for example.  In the latter case, use of
FI_AV_MAP may be able to avoid memory references during data transfer
operations.

For AV's of type FI_AV_TABLE, addresses are placed into the table in
order.  An address is inserted at the lowest index that corresponds to
an unused table location, with indices starting at 0.  That is, the
first address inserted may be referenced at index 0, the second at index
1, and so forth.  When addresses are inserted into an AV table,
the assigned fi_addr values will be simple indices
corresponding to the entry into the table where the address was
inserted.  Index values accumulate across successive insert calls
in the order the calls are made, not necessarily in the order the
insertions complete.

Because insertions occur at a pre-determined index, the fi_addr
parameter may be NULL.  If fi_addr is non-NULL, it must reference
an array of fi_addr_t, and the buffer must remain valid until the
insertion operation completes.  Note that if fi_addr is NULL and
synchronous operation is requested without using FI_SYNC_ERR flag, individual
insertion failures cannot be reported and the application must use
other calls, such as `fi_av_lookup` to learn which specific addresses
failed to insert.  Since fi_av_remove is provider-specific, it is recommended
that calls to fi_av_insert following a call to fi_av_remove always reference a
valid buffer in the fi_addr parameter.  Otherwise it may be difficult to
determine what the next assigned index will be.

If the address vector is configured with authorization keys, the fi_addr
parameter may be used as input to define the authorization keys associated
with the endpoint addresses being inserted. This is done by setting the fi_addr to an
authorization key fi_addr_t generated from `fi_av_insert_auth_key`
and setting the FI_AUTH_KEY flag. If the FI_AUTH_KEY flag is not set, addresses
being inserted will not be associated with any authorization keys. Whether or not
an address can be disassociated with an authorization key is provider
specific. If a provider cannot support this disassociation, an error will be returned.
Upon successful insert with FI_AUTH_KEY flag, the returned fi_addr_t's will map to
endpoint address against the specified authorization keys. These fi_addr_t's can be
used as the target for local data transfer operations.

If the endpoint supports `FI_DIRECTED_RECV`, these fi_addr_t's can be used to
restrict receive buffers to a specific endpoint address and authorization key.

For address vectors configured with FI_AV_USER_ID, all subsequent target events
corresponding to the address being inserted will return FI_ADDR_NOTAVAIL until
the user defines a user ID for this fi_addr_t. This is done by using
fi_av_set_user_id.

*flags*
: The following flag may be passed to AV insertion calls: fi_av_insert,
  fi_av_insertsvc, or fi_av_insertsym.

- *FI_MORE*
: In order to allow optimized address insertion, the application may
  specify the FI_MORE flag to the insert call to give a hint to the
  provider that more insertion requests will follow, allowing the
  provider to aggregate insertion requests if desired.  An application
  may make any number of insertion calls with FI_MORE set, provided
  that they are followed by an insertion call without FI_MORE.  This
  signifies to the provider that the insertion list is complete.
  Providers are free to ignore FI_MORE.

- *FI_SYNC_ERR*
: This flag applies to synchronous insertions only, and is used to
  retrieve error details of failed insertions.  If set, the context
  parameter of insertion calls references an array of integers, with
  context set to address of the first element of the array.
  The resulting status of attempting to insert each address will be
  written to the corresponding array location.  Successful insertions
  will be updated to 0.  Failures will contain a fabric errno code.

- *FI_AV_USER_ID*
: For address vectors configured without FI_AV_USER_ID specified, this
  flag associates a user-assigned identifier with each AV entry that is
  returned with any completion entry in place of the AV's address.
  If a provider does not support FI_AV_USER_ID with insert, requesting
  this flag during insert will result runtime failure.

  Using the FI_AV_USER_ID flag per insert is invalid if the AV was
  opened with the FI_AV_USER_ID or if the corresponding domain was
  configured with FI_AV_AUTH_KEY.

  With libfabric 1.20, users are encouraged to specify the FI_AV_USER_ID
  when opening an AV and use fi_av_set_user_id.

- *FI_AUTH_KEY*
: Denotes that the address being inserted should be associated with the
  passed in authorization key fi_addr_t.

  See the user ID section below.

## fi_av_insertsvc

The fi_av_insertsvc call behaves similar to fi_av_insert, but allows the
application to specify the node and service names, similar to the
fi_getinfo inputs, rather than an encoded address.  The node and service
parameters are defined the same as fi_getinfo(3).  Node should be a string
that corresponds to a hostname or network address.  The service string
corresponds to a textual representation of a transport address.
Applications may also pass in an `FI_ADDR_STR` formatted address as the
node parameter. In such cases, the service parameter must be NULL. See
fi_getinfo.3 for details on using `FI_ADDR_STR`. Supported flags are the
same as for fi_av_insert.

## fi_av_insertsym

fi_av_insertsym performs a symmetric insert that inserts a sequential
range of nodes and/or service addresses into an AV.  The svccnt
parameter indicates the number of transport (endpoint) addresses to
insert into the AV for each node address, with the service parameter
specifying the starting transport address.  Inserted transport
addresses will be of the range {service, service + svccnt - 1},
inclusive.  All service addresses for a node will be inserted before
the next node is inserted.

The nodecnt parameter indicates the number of node (network) addresses
to insert into the AV, with the node parameter specifying the starting
node address.  Inserted node addresses will be of the range {node,
node + nodecnt - 1}, inclusive.  If node is a non-numeric string, such
as a hostname, it must contain a numeric suffix if nodecnt > 1.

As an example, if node = "10.1.1.1", nodecnt = 2, service = "5000",
and svccnt = 2, the following addresses will be inserted into the AV
in the order shown: 10.1.1.1:5000, 10.1.1.1:5001, 10.1.1.2:5000,
10.1.1.2:5001.  If node were replaced by the hostname "host10", the
addresses would be: host10:5000, host10:5001, host11:5000,
host11:5001.

The total number of inserted addresses will be nodecnt x svccnt.

Supported flags are the same as for fi_av_insert.

## fi_av_remove

fi_av_remove removes a set of addresses from an address vector.  All
resources associated with the indicated addresses are released.
The removed address - either the mapped address (in the case of FI_AV_MAP)
or index (FI_AV_TABLE) - is invalid until it is returned again by a
new fi_av_insert.

The behavior of operations in progress that reference the removed addresses
is undefined.

The use of fi_av_remove is an optimization that applications may use
to free memory allocated with addresses that will no longer be
accessed.  Inserted addresses are not required to be removed.
fi_av_close will automatically cleanup any resources associated with
addresses remaining in the AV when it is invoked.

If the address being removed came from `fi_av_insert_auth_key`, the address
will only be removed if all endpoints, which have been enabled against the
corresponding authorization key, have been closed. If all endpoints are not
closed, -FI_EBUSY will be returned. In addition, the FI_AUTH_KEY flag must be
set when removing an authorization key fi_addr_t.

*flags*
: The following flags may be used when removing an AV entry.

- *FI_AUTH_KEY*
: Denotes that the fi_addr_t being removed is an authorization key fi_addr_t.

## fi_av_lookup

This call returns the address stored in the address vector that
corresponds to the given fi_addr.  The returned address is the same
format as those stored by the AV.  On input, the addrlen parameter
should indicate the size of the addr buffer.  If the actual address is
larger than what can fit into the buffer, it will be truncated.  On
output, addrlen is set to the size of the buffer needed to store the
address, which may be larger than the input value.

## fi_rx_addr

This function is used to convert an endpoint address, returned by
fi_av_insert, into an address that specifies a target receive context.
The specified fi_addr parameter must either be a value returned from
fi_av_insert, in the case of FI_AV_MAP, or an index, in the case of
FI_AV_TABLE.  The value for rx_ctx_bits must match that specified in
the AV attributes for the given address.

Connected endpoints that support multiple receive contexts, but are
not associated with address vectors should specify FI_ADDR_NOTAVAIL
for the fi_addr parameter.

## fi_av_straddr

The fi_av_straddr function converts the provided address into a
printable string.  The specified address must be of the same format as
those stored by the AV, though the address itself is not required to
have been inserted.  On input, the len parameter should specify the
size of the buffer referenced by buf.  On output, addrlen is set to the
size of the buffer needed to store the address.  This size may be
larger than the input len.  If the provided buffer is too small, the
results will be truncated.  fi_av_straddr returns a pointer to buf.

## fi_av_insert_auth_key

This function associates authorization keys with an address vector. This
requires the domain to be opened with `FI_AV_AUTH_KEY`. `FI_AV_AUTH_KEY`
enables endpoints and memory regions to be associated with authorization
keys from the address vector. This behavior enables a single endpoint
or memory region to be associated with multiple authorization keys.

When endpoints or memory regions are enabled, they are configured with
address vector authorization keys at that point in time. Later authorization
key insertions will not propagate to already enabled endpoints and memory
regions.

The `auth_key` and `auth_key_size` parameters are used to input the
authorization key into the address vector. The structure of the
authorization key is provider specific. If the `auth_key_size` does not align
with provider specific structure, -FI_EINVAL will be returned.

The output of `fi_av_insert_auth_key` is an authorization key fi_addr_t
handle representing all endpoint addresses against this specific
authorization key. For all operations, including address vector, memory
registration, and data transfers, which may accept an authorization key
fi_addr_t as input, the FI_AUTH_KEY flag must be specified. Otherwise,
the fi_addr_t will be treated as an fi_addr_t returned from the `fi_av_insert`
and related functions.

For endpoints enabled with FI_DIRECTED_RECV, authorization key fi_addr_t's
can be used to restrict incoming messages to only endpoint addresses
within the authorization key. This will require passing in the FI_AUTH_KEY
flag to `fi_recvmsg` and `fi_trecvmsg`.

For domains enabled with FI_DIRECTED_RECV, authorization key fi_addr_t's can
be used to restrict memory region access to only endpoint addresses within the
authorization key. This will require passing in the FI_AUTH_KEY flag to
`fi_mr_regattr`.

These authorization key fi_addr_t's can later be used an input for
endpoint address insertion functions to generate an fi_addr_t for a
specific endpoint address and authorization key. This will require passing in
the FI_AUTH_KEY flag to `fi_av_insert` and related functions.

For address vectors configured with FI_AV_USER_ID and endpoints with
FI_SOURCE_ERR, all subsequent FI_EADDRNOTAVAIL error events will return
FI_ADDR_NOTAVAIL until the user defines a user ID for this authorization key
fi_addr_t. This is done by using fi_av_set_user_id.

For address vectors configured without FI_AV_USER_ID and endpoints with
FI_SOURCE_ERR, all subsequent FI_EADDRNOTAVAIL error events will return the
authorization key fi_addr_t handle.

Flags are reserved for future use and must be 0.

## fi_av_lookup_auth_key

This functions returns the authorization key associated with a fi_addr_t.
Acceptable fi_addr_t's input are the output of `fi_av_insert_auth_key` and
AV address insertion functions. The returned authorization key is in a
provider specific format. On input, the auth_key_size parameter should
indicate the size of the auth_key buffer. If the actual authorization key
is larger than what can fit into the buffer, it will be truncated.  On
output, auth_key_size is set to the size of the buffer needed to store the
authorization key, which may be larger than the input value.

## fi_av_set_user_id

If the address vector has been opened with FI_AV_USER_ID, this function
defines the user ID for a specific fi_addr_t. By default, all fi_addr_t's
will be assigned the user ID FI_ADDR_NOTAVAIL.

*flags*
: The following flag may be passed to AV set user id.

- *FI_AUTH_KEY*
: Denotes that the fi_addr fi_addr_t, for which the user ID is being set for,
  is an authorization key fi_addr_t.

# NOTES

An AV should only store a single instance of an address.
Attempting to insert a duplicate copy of the same address into an AV
may result in undefined behavior, depending on the provider implementation.
Providers are not required to check for duplicates, as doing so could
incur significant overhead to the insertion process. For portability,
applications may need to track which peer addresses have been inserted
into a given AV in order to avoid duplicate entries.  However, providers are
required to support the removal, followed by the re-insertion of an
address.  Only duplicate insertions are restricted.

Providers may implement AV's using a variety of mechanisms.
Specifically, a provider may begin resolving inserted addresses as
soon as they have been added to an AV, even if asynchronous operation
has been specified.  Similarly, a provider may lazily release
resources from removed entries.

# USER IDENTIFIERS FOR ADDRESSES

As described above, endpoint addresses authorization keys that are
inserted into an AV are mapped to an fi_addr_t value. The endpoint
address fi_addr_t is used in data transfer APIs to specify the
destination of an outbound transfer, in receive APIs to indicate
the source for an inbound transfer, and also in completion events to
report the source address of inbound transfers. The authorization key
fi_addr_t are used in receive and MR APIs to resource incoming
operations to a specific authorization key, and also in completion
error events if the endpoint is configured with FI_SOURCE_ERR. The
FI_AV_USER_ID capability bit and flag provide a mechanism by which the
fi_addr_t value reported by a completion success or error event is
replaced with a user-specified value instead. This is useful for
applications that need to map the source address to their own data
structure.

Support for FI_AV_USER_ID is provider specific, as it may not be feasible
for a provider to implement this support without significant overhead.
For example, some providers may need to add a reverse lookup mechanism.
This feature may be unavailable if shared AVs are requested, or
negatively impact the per process memory footprint if implemented.  For
providers that do not support FI_AV_USER_ID, users may be able to trade
off lookup processing with protocol overhead, by carrying source
identification within a message header.

For address vectors opened without FI_AV_USER_ID, user-specified fi_addr_t
values are provided as part of address insertion (e.g. fi_av_insert)
through the fi_addr parameter. The fi_addr parameter acts as input/output
in this case.  When the FI_AV_USER_ID flag is passed to any of the insert
calls, the caller must specify an fi_addr_t identifier value to associate
with each address.  The provider will record that identifier and use it
where required as part of any completion event.  Note that the output from
the AV insertion call is unchanged.  The provider will return an fi_addr_t
value that maps to each address, and that value must be used for all data
transfer operations.

For address vectors opened with FI_AV_USER_ID, fi_av_set_user_id is used
to defined the user-specified fi_addr_t.

# RETURN VALUES

Insertion calls, excluding `fi_av_insert_auth_key`, for an AV opened for
synchronous operation will return the number of addresses that were
successfully inserted.  In the case of failure, the return value will be
less than the number of addresses that was specified.

Insertion calls, excluding `fi_av_insert_auth_key`, for an AV opened for
asynchronous operation (with FI_EVENT flag specified) will return FI_SUCCESS
if the operation was successfully initiated. In the case of failure, a
negative fabric errno will be returned.  Providers are allowed to abort
insertion operations in the case of an error. Addresses that are not inserted
because they were aborted will fail with an error code of FI_ECANCELED.

In both the synchronous and asynchronous modes of operation, the fi_addr
buffer associated with a failed or aborted insertion will be set to
FI_ADDR_NOTAVAIL.

All other calls return FI_SUCCESS on success, or a negative value corresponding
to fabric errno on error. Fabric errno values are defined in `rdma/fi_errno.h`.

# SEE ALSO

[`fi_getinfo`(3)](fi_getinfo.3.html),
[`fi_endpoint`(3)](fi_endpoint.3.html),
[`fi_domain`(3)](fi_domain.3.html),
[`fi_eq`(3)](fi_eq.3.html)
