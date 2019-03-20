# Service Replication

Pool and container services are made highly available by replicating their stateâpool and container metadataâusing Raft-based consensus and strong leadership. A service replicated in this generic approach tolerates the failure of any minority of its replicas. By spreading replicas of each service across the fault domains, pool and container services can therefore tolerate a reasonable number of target failures.

<a id="8.3.1"></a>
## Architecture

A replicated service is built around a Raft replicated log. The service transforms RPCs into state queries and deterministic state updates. All state updates are committed to the replicated log first, before being applied by any of the service replicas. Since Raft guarantees consistency among log replicas, the service replicas end up applying the same set of state updates in the same order and go through identical state histories.

Among all replicas of a replicated service, only the current leader can handle service RPCs. The leader of a service is the current Raft leader (i.e., a Raft leader with the highest term number at the moment). Non-leader replicas reject all service RPCs and try to redirect the clients to the current leader to the best of their knowledge. Clients cache the addresses of the replicas as well as who current leader is. Occasionally, a client may not get any meaningful redirection hints and can find current leader by communicating to a random replicas.

The <a href="#f8.1">figure</a> below shows the modules constituting a service replica. The service module handles RPCs by transforming them into state queries and deterministic state updates. The Raft module implements the replicated log following the Raft protocol, by communicating with Raft modules on other replicas. It provides methods for the service module to perform the queries and the updates. The storage module, which in this case is the persistent memory and the file system, stores the service and Raft state. It uses the NVM library to update the state stored in persistent memory atomically.

<a id="f8.1"></a>
**Service replication modules**

![../../doc/graph/Fig_041.png](../../doc/graph/Fig_041.png "Service replication modules")

<a id="8.3.2"></a>
## RPC Handling

When an RPC request arrives at the leader, a service thread of the service module picks up the request and handles it by executing a handler function designed for this type of request. As far as service replication is concerned, a handler comprises state queries (e.g., reading the epoch state), state updates (writing a new version of the pool map), and RPCs to other services (e.g., TARGET_CONTAINER_OPEN RPCs sent to target services). Some handlers involve only queries, some involve updates as well as queries, and others involve all three kinds of actions; rarely, if ever, do handlers involve only updates but no queries.

A handler must assemble all its updates into a single log entry, commit the log entry, and wait for the log entry to become applicable before applying the updates to the service state. Using a single log entry per update RPC easily makes each update RPC atomic with regard to leader crashes and leadership changes. If RPCs that cannot satisfy this requirement are introduced in the future, additional transaction recovery mechanisms will be required. A leaderâs service state therefore always represents the effects of all completed update RPCs this leader has handled so far.

Queries, on the other hand, can read directly from the service state, without going through the replicated log. However, to make sure a request sees the effects of all completed update RPCs handled by all leaders ever elected, the handler must ask the Raft module whether there has been any leadership changes. If there has been none, all queries made for this request so far are not stale. If the leader has lost its leadership, the handler aborts the request with an error redirecting the client to the new leader.

RPCs to other services, if they update state of destination services, must be idempotent. In case of a leadership change, the new leader may send them again, if the client resent the service request in question.

Handlers need to cope with reasonable concurrent executions. Conventional local locking on the leader is sufficient to make RPC executions linearizable. Once a leadership change happens, the old leader can no longer perform any updates or leadership verifications with-out noticing the leadership change, which causes all RPCs in execution to abort. The RPCs on the new leader are thus not in conflict with those still left on the old leader. The locks therefore do not need to be replicated as part of the service state.

<a id="8.3.3"></a>
## Service Management

The pool service maintains a record of every container service in the container service index (<a href="#8.2.1">Pool Service</a>) and manages the configuration (i.e., the set of replicas) of every one of them, as well as that of itself. This involves three cases:

<ol>
<li>Creating a new container. The pool service decides whether the new container uses an existing container service or a new one. If an existing one suffices, the pool service simply makes state updates to the leader of the existing container service. If a new container service is desired, the pool service decides which targets shall host the container service replicas and creates a single replica first. Other replicas are then added through configuration changes. Once all replicas are added, the pool service records the replica addresses in the container service index and makes the state updates to initialize the container metadata.
</li>
<li>
Responding to a RAS event. Before disabling the target declared dead by this RAS event, the pool service checks:
</li>
<ol>
<li>
if the target hosts any (pool or container) service replicas. Any affected replicas are replaced with new ones created on other targets and removed from corresponding services. This is achieved by a configuration change following the original Raft protocol.
</li>
<li>
If any of the replicas is a leader, the configuration change will also result in a leadership transfer (following again the original raft protocol).
</li>
</ol>
  When the configuration changes are done, the pool service records the new configurations in the container service index and updates the pool map to disable the target. It is worth noting that the pool service records the configuration of every Raft instance only as a hint for clients to find the Raft leader and any service membership change is handled through Raft.
<li>
Executing a target exclusion request from a caller. The pool service handles this case in the same way as it handles case 2.
</li>
</ol>
In all the cases, the decision making is based on the definitive pool map maintained by the pool service, with the goal that the replicas of a service shall belong to different fault domains when possible, so that they are unlikely to fail together. And, the decisions are executed through configuration changes that follow Raftâs single-server membership change protocol. (The alternative arbitrary membership change protocol may also be considered in the future if it proves to offer meaningful performance benefits.)

In the remainder of this section, the addresses of the replicas belonging to a replicated service are collectively referred to as the service address. Pool and container services are assumed to be highly available when discussing higher-level protocols, because the service replication internals are largely transparent and irrelevant.
