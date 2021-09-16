# Replicated Services

Module `rsvc` implements a generic replicated service framework. This file covers service replication in general, before focusing specifically on module `rsvc`.

## Introduction

Certain DAOS RPC services, such as Pool Service (`pool_svc`), and Container Service (`cont_svc`), are replicated using the state machine approach with Raft. Each of these services tolerates the failure of any minority of its replicas. By spreading its replicas across different fault domains, the service can be highly available. Since this replication approach is self-contained in the sense that it requires only local persistent storage and point to point unreliable messaging, but not any external configuration management service, these services are mandatory for bootstrapping DAOS systems as well as managing the configuration of the lighter-weight I/O replication protocol.

### Architecture

An RPC service handles incoming _service requests_ (or just _requests_) based on its current _service state_ (or just _state_). To replicate a service is, therefore, to replicate its state so that each request is handled based on the state reached through all prior requests.

The state of a service is replicated with a Raft log. The service transforms requests into state queries and deterministic state updates. All state updates are committed to the Raft log first, before being applied to the state. Since Raft guarantees the consistency among the log replicas, the service replicas end up applying the same set of state updates in the same order and go through identical state histories.

Raft adopts a strong leadership design, so does each replicated service. A service leader of a term is the Raft leader of the same Raft term. Among the replicas of a service, only the leader of the highest term can handle requests. For the server side, the service code is similar to that of a non-replicated RPC service, except for the handling of leadership change events. For the client side, the service requests must be sent to the current leader, which must be searched for if not known already.

A replicated service is implemented using a stack of modules:

	[ pool_svc, cont_svc, ... ]
	[ ds_rsvc ]
	[                rdb                ]
	[ raft ]
	[                vos                ]

`pool_svc`, and `cont_svc` implement the request handlers and the leadership change event handlers of the respective services. They define their respective service state in terms of the RDB data model provided by `rdb`, implement state queries and updates using RDB transactions, and register their leadership change event handlers into the framework `rsvc` offers.

`rdb` (`daos_srv/rdb`) implements a hierarchical key-value store data model with transactions, replicated using Raft. It delivers Raft leadership change events to `ds_rsvc`, implements transactions using the Raft log, and stores a service's data model and its own internal metadata using the VOS data model. `rdb` on the leader replica, interfaces with VOS to monitor available persistent storage and, when free space drops below a threshold, rejects new transactions before appending entries to the Raft log that could otherwise result in the service becoming unavailable (due to a mix of successful and "out of space" failures in applying the entries). It also interfaces with VOS to periodically compact storage by triggering aggregation of older versions (epochs) of the log.

`raft` (`rdb/raft/include/raft.h`) implements the Raft core protocol in a library. Its integration with VOS and CaRT is done inside `rdb` via callback functions.

A replicated service client (e.g., `dc_pool`) uses `dc_rsvc` to search for the current service leader:

	[ dc_pool ]
	[ dc_rsvc ]

The search is accomplished with a combination of a client maintained list of candidate service replicas and server RPC error responses in some cases containing a hint where the current leader may be found. A server not running the service responds with an error a client uses to remove that server from its list. A server acting as a non-leader replica responds with a different error, including a hint a client uses to (if necessary) add to its list and alter its search for the leader. And, `dc_rsvc` both at client startup and later when the client's list of candidate service replicas may become empty (e.g., due to membership changes in the service), contacts one of the DAOS servers running on a management service node to get an up-to-date list of service replicas for the pool.

## Module `rsvc`

The main purpose of `rsvc` is to avoid code duplication among different replicated service implementations. The callback-intensive API follows from the attempt to extract as much common code as possible, even at the expense of API simplicity. This is a key difference from how other module APIs are designed.

`rsvc` has two parts:

* `ds_rsvc` (`daos_srv/rsvc.h`): server-side framework.
* `dc_rsvc` (`daos/rsvc.h`): client-side library.

`dc_rsvc` is currently still called `rsvc`. A rename will be done in a future patch.
