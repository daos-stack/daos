<a id="4.3"></a>
# Fault Model

DAOS relies on massively distributed single-ported storage. Each target
is thus effectively a single point of failure. DAOS achieves availability
and durability of both data and metadata by providing redundancy across
targets in different fault domains. DAOS internal pool and container
metadata are replicated via a robust consensus algorithm. DAOS objects
are then safely replicated or erasure-coded by transparently leveraging
 the DAOS distributed transaction mechanisms internally. The purpose of
this section is to provide details on how DAOS achieves fault tolerance
 and guarantees object resilience.

<a id="4.3.1"></a>
## Hierarchical Fault Domains

A fault domain is a set of servers sharing the same point of failure and
which are thus likely to fail altogether. DAOS assumes that fault domains
are hierarchical and do not overlap. The actual hierarchy and fault domain
membership must be supplied by an external database used by DAOS to
generate the pool map.

Pool metadata are replicated on several nodes from different high-level
fault domains for high availability, whereas object data can be replicated
or erasure-coded over a variable number of fault domains depending on
the selected object class.

<a id="4.3.2"></a>
## Fault Detection

DAOS engines are monitored within a DAOS system through a gossip-based protocol
called [SWIM](https://doi.org/10.1109/DSN.2002.1028914)
that provides accurate, efficient, and scalable fault detection.
Storage attached to each DAOS target is monitored through periodic local
health assessment. Whenever a local storage I/O error is returned to the
DAOS server, an internal health check procedure will be called automatically.
This procedure will make an overall health assessment by analyzing the
IO error code and device SMART/Health data. If the result is negative,
the target will be marked as faulty, and further I/Os to this target will be
rejected and re-routed.

<a id="4.3.3"></a>
## Fault Isolation

Once detected, the faulty target or engine (effectively a set of targets)
must be excluded from the pool map. This process is triggered either manually
by the administrator or automatically. Upon exclusion, the new version of
the pool map is eagerly pushed to all storage targets. At this point, the pool
enters a degraded mode that might require extra processing on access (e.g.
reconstructing data out of erasure code). Consequently, DAOS client and storage
nodes retry an RPC until they find an alternative replacement target
from the new pool map or experiences an RPC timeout. At this point,
all outstanding communications with the
evicted target are aborted, and no further messages should be sent to the
target until it is explicitly reintegrated (possibly only after maintenance
action).

All storage targets are promptly notified of pool map changes by the pool
service. This is not the case for client nodes, which are lazily informed
of pool map invalidation each time they communicate with any engines. To do so,
clients include their last known pool map version with every RPC and servers reply
with the current pool map version. Consequently, when a DAOS client
experiences RPC timeout, it regularly communicates with the other DAOS
target to guarantee that its pool map is always current. Clients will then
eventually be informed of the target exclusion and enter into degraded mode.

This mechanism guarantees global node eviction and that all nodes eventually
share the same view of target aliveness.

<a id="4.3.4"></a>
## Fault Recovery

Upon exclusion from the pool map, each target starts the rebuild process
automatically to restore data redundancy. First, each target creates a list
of local objects impacted by the target exclusion. This is done by scanning
a local object table maintained by the underlying storage layer. Then for
each impacted object, the location of the new object shard is determined and
redundancy of the object restored for the whole history (i.e., snapshots).
Once all impacted objects have been rebuilt, the pool map is updated a second
time to report the target as failed out. This marks the end of collective
rebuild process and the exit from degraded mode for this particular fault.
At this point, the pool has fully recovered from the fault and client nodes
can now read from the rebuilt object shards.

This rebuild process is executed online while applications continue accessing
and updating objects.

### Engine Self-Termination and Automatic Restart

A DAOS engine may be excluded from the group map because of inactivity
for example. When an engine becomes aware of it's removal from the
group map it will self-terminate to protect data integrity and system stability.

When an engine self terminates, it raises a `engine_self_terminated` RAS event
(INFO_ONLY, NOTICE severity) containing the rank and incarnation information.
The control plane automatically handles this event by:

1. Detecting the engine self terminated event through the RAS event system
2. Identifying the engine instance associated with the rank
3. Waiting for the engine process to fully stop
4. Automatically restarting the engine to rejoin the system

This automatic restart mechanism is implemented in all control servers to ensure
local engine recovery happens regardless of management service leadership state.
The restarted engine will rejoin the system with a new incarnation number and
resume normal operations.

This self-healing mechanism allows DAOS to automatically recover system
membership state from transient engine failures without administrator
intervention, improving overall system availability.

#### Rate Limiting

To prevent restart storms and ensure system stability, automatic engine restarts
are rate-limited on a per-rank basis. By default, a minimum delay of 300 seconds
(5 minutes) is enforced between consecutive restart attempts for the same rank.
If an engine self-terminates again before this delay expires, the restart request
is rejected and logged at NOTICE level.

The rate-limiting interval can be customized by setting the
`engine_auto_restart_min_delay` configuration option (in seconds) in the
daos_server.yml file. For example:

```yaml
engine_auto_restart_min_delay: 600  # 10 minutes between restarts
```

This protection mechanism prevents scenarios where:
- Repeated transient failures cause excessive restart cycling
- A misconfigured engine continuously self-terminates
- Cascading failures overwhelm the control plane with restart requests

#### Disabling Automatic Restart

The automatic restart behavior can be completely disabled by setting the
`disable_engine_auto_restart` configuration option to `true` in the
daos_server.yml file:

```yaml
disable_engine_auto_restart: true
```

When auto restart is disabled, engines that self-terminate will not be
automatically restarted by the control plane, requiring manual intervention
to restart the affected engine instances. This setting may be useful for
debugging scenarios or when custom external restart management is preferred.
