---
layout: page
title: fi_profile(3)
tagline: Libfabric Programmer's Manual
---
{% include JB/setup %}

# NAME

fi_profile \- Fabric Profiling Interfeace

fi_profile_open / fi_profile_close
: Open/close a profile object on an endpoint or a domain specified by the fid 
or associated with the profile object.

fi_profile_query_vars / fi_profile_query_events
: Query available profiling variables/events associated the profile object.

fi_profile_read_u64
: Read the value of a FI_UINT64-typed variable.

fi_profile_reg_callback
: Register a callback function to a profiling event.

fi_profile_start_reads / fi_profile_end_reads
: Control consecutively accessing on multiple variables at the same time.

# SYNOPSIS

```c
include <rdma/fi_profile.h>

int fi_profile_open(struct fid *fid, uint64_t flags,
    struct fid_profile **prof_fid, void *context);

int fi_profile_close(struct fid_profile *prof_fid);

void fi_profile_reset(struct fid_profile *prof_fid,  uint64_t flags);

ssize_t fi_profile_query_vars(struct fid_profile *prof_fid,
    struct fi_profile_desc *varlist, size_t *count)

ssize_t fi_profile_query_events(struct fid_profile *prof_fid,
    struct fi_profile_desc *eventlist, size_t *count);

int fi_profile_register_callback(struct fid_profile *prof_fid,
    uint32_t event_id,
    int (*callback)(struct fid_profile *prof_fid, 
	                struct fi_profile_desc *event, void *param, 
	                size_t size, void *context),
    void *context);

sszie_t fi_profile_read_u64(struct fid_profile *prof_fid, uint32_t var_id,
    uint64_t *data);

void fi_profile_start_reads(struct fid_profile *prof_fid, uint64_t flags);

void fi_profile_end_reads(struct fid_profile *prof_fid, uint64_t flags);

```

# ARGUMENTS

*fid*
: Reference to an endpoint or a domain which is the target for profiling.

*prof_fid*
: Reference to a profile object associating with the target fid.

*flags*
: Flags to control profiling or operation.

*context*
: Context associated with the profiling operations.

*varlist*
: An array of profiling variables.

*eventlist*
: An array of profiling events.

*count*
: Number of variables or events available in the provider.

*var_id*
: An unique ID for a variable.

*event_id*
: An unique ID for an event.

*callback*
: Callback function registered for an event. See below callback description.

*data*
: The current value of a variable.

# DESCRIPTION

Fabric profiling interface allows users to obtain configuration or runtime
properties from the providers in the form of "variables". Providers 
define variables and make them readable to users through the profiling 
interface. Providers define events and use them to report when an operation 
has occurred or a state changed where one or more variables may change their 
values. Users can register callback functions to one or more events, and 
receive notifications from providers once an event occurs.

To support the profiling interface, an endpoint or a domain in a provider 
needs to maintain a fid_profile object, and implement the profiling operations
there. To start a profiling, an open call must be made on the target endpoint 
or domain object to obtain the fid_profile object. If the target object does 
not support profiling, the open call will fail with the error code -FI_ENOSYS. 
Once a fid_profile object is obtained successfully, it can be used to query 
available variables or events and read variables' values.

# VARIABLES

The profiling variables are defined in providers and are accessible through 
the profiling interface. Variables are specified using struct 
fi_profile_desc described below. A variable can be a configuration setting, 
a runtime state, or a counter associated with an endpoint or a domain. 
Some variables are common and available in most providers, while others are 
provider-specific and are only available in one provider. Users can query 
available variables and their values using the profiling interface.

Provider specific variables are outside the scope of this man page. libfabric 
defined variables and their datatypes are described below.

## FI_VAR_UNEXP_MSG_CNT (data type: uint64_t)

This variable returns the current number of unexpected messages that have been
received by the provider which are waiting for matching receive buffers.

## FI_VAR_UNEXP_MSG_QUEUE (data type: struct fi_cq_err_entry)

This variable returns the current unexpected message queue information, each 
entry in the queue is for an unexpect message presented in fi_cq_err_entry
structure.

# EVENTS

Profiling events are defined to notify users that an operation has occurred or 
the state of an object has changed, which typically indicate that one or more 
variables have been modified. Events are specified using struct 
fi_profile_desc described below. Similar to variables, events may be 
pre-defined by libfabric or be provider specific. Users can register a 
callback to be notified when an event occurs. libfabric defined events and 
the data that corresponds with the event, if any, are described below.

Applications may read variables' values from the event callback when allowed,
but may not register or unregister for events, query for profiling variables 
or events.

libfarbic pre-defined events include:

## FI_EVENT_UNEXP_MSG_RECVD 

This event indicates that an unexpected message was received and has been
queued by the provider. 

## FI_EVENT_UNEXP_MSG_MATCHED

This event indicates that an unexpected messages that was previously queued
by the provider has been matched. 

These two events can notify a user without any data and rely on the user 
to read variables, for example, FI_VAR_UNEXP_MSG_CNT, to get the current length
of the unexpected message queue. It also can notify a user along with data 
of variable, for example,  FI_VAR_UNEXP_MSG_QUEUE, to provide the messages
information about the current unexpected message queue.

# PROFILE DESCRIPTION

Variables and events are described using struct fi_profile_desc: 

{% highlight c %}
enum fi_profile_type {
	fi_primitive_type,
	fi_defined_type,
};

struct fi_profile_desc {
	uint32_t id;
	enum fi_profile_type datatype_sel;
	union {
		enum fi_datatype primitive;
		enum fi_type defined;
	} datatype;
	uint64_t flags;
	size_t size;
	const char *name;
	const char *desc;
};
{% endhighlight %}

## id

Each variable or event has a unique identifier (id). Providers provide this 
id and users use it to get the variable value or register callbacks for 
the event.

## datatype

The data type of the variable. It can be either a primitive type defined in
*enum fi_datatype*, or a libfabric structure type defined in *enum fi_type*.

## datatye_sel

The selector of data type. It is used to indicate the data type is from
*enum fi_datatype* or from *enum fi_type*.

## flags

The features or restrictions of the variable. 

## size

The size (in bytes) of the data.

## name

Reference to the name of a variable or an event. Storage is allocated and 
managed by the provider. Variables or events with the same name should have 
an identical id. 

## desc
Reference to the description of a variable or an event. Storage is allocated
and managed by the provider.

# fi_profile_open

fi_profile_open is used to initiate a profiling on a target endpoint or domain 
specified by the fid. Upon retuning successfully, a fid_profile is returned 
and can be used to perform profiling operations.  If the target endpoint or 
domain does not support fabric profiling interface, the open call will fail 
with error code -FI_ENOSYS.

# fi_profile_close

Close the profiling operations and release related resources.

# fi_profile_query_vars / fi_profile_query_events

fi_profile_query_vars and fi_profile_query_events calls can be used to query 
available variables or events from the target endpoint or domain associated 
with the profile object. On input, the buffer ("varlist" or "eventlist") 
should be allocated, and the "count" indicates the maximum number of entries 
(variables or events) allowed in the buffer. On output, the "count" indicates 
the actual number of varibles or events available in the target endpoint or
domain. If the buffer is NULL, only the "count" will be set and return.
User can use the "count" to allocate space and call the function again.
The return value of these calls indicates the number of entries returned
the buffer.

# fi_profile_reg_callback

fi_profile_reg_callback registers a callback function to the event. The event 
should be in the event list obtained using the fi_profile_query_events, 
otherwise, an error code -FI_EINVAL will be returned. 

## Callback

It is used to receive notifications from a provider when an event occurs.
A callback can also take optional parameters that allows a provider 
to push data to the user when an event occurss.

A callback is defined by the user and called by a provider as an additional
operation, normally before or after data transfer operation. This will 
increase the overhead of the operation. Callback functions must avoid having 
blocking calls or taking non-trival time. And callback functions should 
limit interactions with a provider to reading the variables only if allowed.

The signature of the callback function is:

{% highlight c %}
int (*callback)(struct fid_profile *prof_fid, struct fi_profile_desc *event,
                void *param, size_t size, void *context)
 {% endhighlight %}

*prof_fid*
: Reference to the profile object.

*event*
: The description of event which the callback has been registered.

*param*
: An optional parameter passed to the callback associated with the event.
  The data type and size of the param are defined in the event description.

*size*
: The amount of the data pointed by *param* if *param* is not NULL.

*context*
: The application context passed when the callback is registered.

# fi_profile_read_u64

fi_profile_read_u64 is a wrapper function for FI_UINT64-type variables.
It returns an uint64_t value of the specified variable.  The variable should 
be in the variable list obtained using fi_profile_query_vars, otherwise, 
an error code -FI_EINVAL will be returned. 

# fi_profile_start_reads / fi_profile_end_reads

fi_profile_start_reads and fi_profile_end_reads calls are used when trying to 
read multiple variables at the same time. Variables read in between these two 
calls are expected to have values from a single snapshot.

# NOTES

Users should call fi_profile_close to release all resources allocated for 
profiling in the provider.

# RETURN VALUES

Returns 0 on success.  On error, a negative value corresponding to fabric 
errno is returned. If the provider does not support profiling interface, 
a -FI_ENOSYS will be returned. For fi_profile_query_vars and 
fi_profile_query_events, a positive return value indicates the number of 
variables or events returned in the list. 

Fabric errno values are defined in `rdma/fi_errno.h`.
