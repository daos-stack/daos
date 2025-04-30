# DAOS Telemetry Library

DAOS includes a library for instrumentation of the I/O servers (engines) and clients. On the
server side, the telemetry is always enabled. Client telemetry may be optionally enabled and
is described in a <a href="/src/client/telemetry.md">separate document</a>.

Before moving on, a quick point of clarification: In the context of these documents, the term
"telemetry" refers exclusively to metrics that have been predefined in DAOS to measure certain
characteristics of the system. Examples of these metrics might include bytes read, counts
of operations by type, counts of errors, etc. This documentation does not cover logging or
log analysis. External integrations may define additional metrics based on log analysis, but
that topic is not covered here.

## Unmanaged Use Cases

On the server side, the only use cases that don't involve some metrics infrastructure are
primarily aimed at DAOS developers who need an easy way to spot-check metrics values while
debugging or working on a new feature. The tools described in this section are not recommended
for production uses, as there are better solutions available.

### Local Metrics Dump via daos_metrics

The `daos_metrics` utility is typically provided as part of a DAOS server installation and may
be used to directly access the shared memory segments exposed by the `daos_engine` process(es)
running on a server. The user running `daos_metrics` must have read permission to the shared
memory exposed by the engine. In most cases, this will just be the same user used by the
server processes.

With no arguments, `daos_server` will attempt to traverse and pretty-print the telemetry tree
rooted at engine index 0, which is associated with the first engine started by the `daos_server`
control process. If the tool should traverse a different engine's telemetry, specify the index
with the `--srv_idx, -S` option.

By default, the tool will pretty-print the metrics in a format that is easy for humans to read
but takes up a lot of screen real estate. An example of the first few metrics printed looks
like the following:
```
ID: 0
    started_at: Thu May  1 20:13:26 2025
    servicing_at: Thu May  1 20:13:29 2025
    rank: 0
    events
        dead_ranks: 0 events
        last_event_ts: Thu Jan  1 00:00:00 1970
    net
        uri
            lookup_self: 0
            lookup_other: 0
```

Another output option is to print the metrics in 
<a href="https://en.wikipedia.org/wiki/Comma-separated_values">CSV</a> format, where each
metric is represented in a single spreadsheet row. This format is a bit easier to sift through,
but requires some more understanding of the metrics to make sense of them. Running the same
command with the `--csv, -C` option produces output like the following:
```
name,value,min,max,mean,sample_size,sum,std_dev
ID: 0/started_at,Thu May  1 20:13:26 2025
ID: 0/servicing_at,Thu May  1 20:13:29 2025
ID: 0/rank,0
ID: 0/events/dead_ranks,0
ID: 0/events/last_event_ts,Thu Jan  1 00:00:00 1970
ID: 0/net/uri/lookup_self,0
ID: 0/net/uri/lookup_other,0
```

Refer to the output of `daos_metrics --help` for a full list of options.

### Remote Metrics Dump via dmg

If the `daos_server` configuration specifies a `telemetry_port: <port>` parameter, then each
`daos_server` process will start a HTTP server with a special `/metrics` endpoint that exposes
the engine metrics in the 
<a href="https://github.com/prometheus/docs/blob/main/content/docs/instrumenting/exposition_formats.md">Prometheus exposition format</a>. The `dmg` tool provided as part of the DAOS admin installation
includes a `telemetry` subcommand that can be used to list or query the metrics on a server. Note
that this tool only supports interaction with a single server at a time -- it is not intended to
provide aggregate metrics across a cluster of servers.

An example of the query output for the same server used in the previous examples looks like the
following (note that port may be omitted if the server is running on port 9191):
```
$ dmg telemetry metrics query -l localhost -m engine_started_at
connecting to localhost:9191...
- Metric Set: engine_started_at (Type: Gauge)
  Timestamp of last engine startup
    Metric Labels   Value
    ------ ------   -----
    Gauge  (rank=0) 1.746130406e+09
    Gauge  (rank=1) 1.746130404e+09
    Gauge  (rank=2) 1.746130405e+09
```

For completeness, we can see that curl may also be used, in a pinch:
```
$ curl -s http://localhost:9191/metrics | grep engine_started_at
# HELP engine_started_at Timestamp of last engine startup
# TYPE engine_started_at gauge
engine_started_at{rank="0"} 1.746130406e+09
engine_started_at{rank="1"} 1.746130404e+09
engine_started_at{rank="2"} 1.746130405e+09
```

Note that the metric has the `engine_` prefix in its name and follows the Prometheus conventions
for naming and labeling.

## Managed Use Cases

For the purposes of this document, a full discussion of how to set up a monitoring solution for
DAOS is out of scope. The details and technologies are a constantly-moving target, and most sites
already have existing infrastructure into which DAOS monitoring should be integrated. Instead of
going to that level of detail, this section will describe some high-level concepts and offer
guidance on various integration strategies.

### The Prometheus Exporter

As documented in <a href="/src/control/lib/telemetry/promexp/README.md">telemetry/promexp</a>,
the `daos_server` and `daos_agent` processes may be configured to start a HTTP server with a
`/metrics` endpoint that exposes telemetry for engine processes hosted on that server. The
simplest way to use this endpoint is to install a single
<a href="https://prometheus.io/docs/prometheus/latest/getting_started/">Prometheus Server</a>
instance that is configured to periodically "scrape" the metrics endpoints on all of the
`daos_server` and/or `daos_agent` processes in the cluster. The metrics are then efficiently
stored in its time series database for later query and analysis (e.g. with a
<a href="https://grafana.com/">Grafana</a> dashboard that is based on the
<a href="/utils/grafana/README.md">example</a> provided with DAOS).

Aside from Prometheus itself, the <a href="https://github.com/prometheus/docs/blob/main/content/docs/instrumenting/exposition_formats.md">Prometheus exposition format</a> is legible to most
modern time series data ingestion and storage solutions. Prominent examples of these include:
  * <a href="https://www.timescale.com/">TimescaleDB</a>
  * <a href="https://www.influxdata.com/">InfluxDB</a>
  * <a href="https://victoriametrics.com/">VictoriaMetrics</a>

The specifics of how these solutions would collect metrics (e.g. pull from a central server vs.
push from local agents) are outside the scope of this document.

### Custom Exporter

The Prometheus Exporter provides a solution that requires minimal understanding of and
integration with the DAOS telemetry library. The tradeoff for this simplicity is that
the metrics must be converted into an intermediate format and then converted into the final
storage format. At small scales, these inefficiencies may be of little concern, but for larger
scales, it may be desirable to implement a custom solution for reading the metrics directly
out of shared memory and converting them into an efficient wire format.

One existing example of this approach is the <a href="https://github.com/ovis-hpc/ldms/tree/main/ldms/src/contrib/sampler/daos">LDMS sampler for DAOS</a>.

# Developer Guide

This section is intended to provide an overview of the telemetry library for DAOS developers who
are interested in adding new instrumentation to the client and/or server implementations.

## Orientation

The library is defined in three header files, 
<a href="/src/include/gurt/telemetry_common.h">&lt;gurt/telemetry_common.h&gt;</a>,
<a href="/src/include/gurt/telemetry_consumer.h">&lt;gurt/telemetry_consumer.h&gt;</a>, and
<a href="/src/include/gurt/telemetry_producer.h">&lt;gurt/telemetry_producer.h&gt;</a>. It is
implemented in a single C file: <a href="/src/gurt/telemetry.c">src/gurt/telemetry.c</a>, and
cmocka unit tests for the library may be found in
<a href="/src/gurt/tests/test_gurt_telem_producer.c">src/gurt/tests/test_gurt_telem_producer.c</a>.

The original design and implementation of the library was based on server-only use cases that
rely on use of shared memory to allow realtime sampling of the metric values from outside of the
engine processes. In the years since that original implementation was completed, the library has
been updated to support client-side use cases, some of which do not need shared memory and instead
use regular memory allocations for metrics.

In all cases, the metrics are managed as nodes in a rooted tree that allows for efficient traversal
and dynamic addition and removal of branches (e.g. for pools).

## Metric Types

Within the library, the smallest unit of organization is a tree node (`struct d_tm_node_t`). Each
node has a type, and depending on the type will have different uses of the fields within the struct.

The full set of supported node types is defined in 
<a href="/src/include/gurt/telemetry_common.h">&lt;gurt/telemetry_common.h&gt;</a>, and
the following list describes the most commonly used types:

  * DIRECTORY: Not a metric per se, but a special node type with a name, a potential child, a
    potential neighbor, and possibly an associated memory region if it is the root of a dynamic branch.
  * TIMESTAMP: Used to record timestamps for events, e.g. engine start time, 
    container mount time, etc.
  * DURATION: Measures the duration of time between recorded start and end times.
  * COUNTER: Used to maintain a uint64 count of something (e.g. bytes read, RPCs sent, etc.).
    Should never be decremented. Consumers must handle wraparound (i.e. reset to zero). When sampled
    over time, may be used to calculate rates of change in a value. Think of this type like the
    odometer in a car. It always goes up, never backwards (unless it wraps around).
  * GAUGE: Simple float64 gauge that records an instantaneous measurement. May be
    incremented, decremented, or reset to zero. Think of this type like a thermometer or
    speedometer in a car. It provides a reading at a point in time and can fluctuate.
  * STATS_GAUGE: Complex gauge that includes an instantaneous measurement along with
    associated statistics (min/max/sum/avg/std_dev/sum_sq/count).

In addition to these core metric types, the DURATION and GAUGE metrics may optionally be associated
with a set of histogram buckets containing counters that correspond to bucketed values set on
the base metric.

## Adding A New Metric

For an exhaustive reference and set of examples, see the
<a href="/src/gurt/examples/telem_producer_example.c">producer</a> and
<a href="/src/gurt/examples/telem_consumer_example.c">consumer</a> examples.

This section will provide a simple(?) example of adding a new counter metric to the engine, with
some commentary and context that may not be readily apparent from reading the code directly.

Let's assume that we have identified a need to count the number of RAS events sent from
the engine up to its local control plane server. The first order of business is to find a
central location through which all RAS events pass in order to count them. Looking at the
engine code, it seems like `send_event()` in <a href="/src/engine/drpc_ras.c">drpc_ras.c</a>
might be a good place to start. There's a problem, though: We need some place to store the
metric, and this function doesn't have access to anything other than the event itself. In
fact, this whole file seems to consist of various wrappers around `dss_drpc_call()` in
<a href="/src/engine/drpc_client.c">drpc_client.c</a>. OK, so let's look there.

Looking in `dss_drpc_call()`, we can see that the function is responsible for sending the
dRPC request immediately if it doesn't need a response, or scheduling it to run on a new
thread if it does. Either way, we still don't have something to "hang" the new metric on.

What now? This has gotten complicated in a hurry. Rummaging around a bit in the engine
directory, we can see that there's a <a href="/src/engine/srv_metrics.c">srv_metrics.c</a>
file. Looks like there's a global `struct engine_metrics` instance which is defined in
<a href="/src/engine/srv_internal.h">srv_internal.h</a>. Putting aside the question of
whether or not it's a good idea to be using a global variable for this kind of thing, let's
just add our new metric there and call it a day.

First, we need to add a new `struct d_tm_node_t` pointer field to `struct engine_metrics`.
Let's name it `drpc_ras_events`. Next, we need to initialize it, which takes care of
allocating telemetry memory and adding the node to the tree. Going back to
`srv_metrics.c`, we can see that there's a `dss_engine_metrics_init()` function that looks
like a good spot to do this. Following the examples set by the existing metrics, we add
some new code that looks like the following:
```C
rc = d_tm_add_metric(&dss_engine_metrics.drpc_ras_events,
                     D_TM_COUNTER,
                     "Number of engine RAS events reported", "events",
                     "events/ras");
```

Of note, we're using the `COUNTER` type, because the value of this metric will always
be incremented, never decremented. If we were adding a metric to measure the current
value of _outstanding_ (i.e. not handled yet) RAS events, then a `GAUGE` would be more
appropriate. But we're not doing that. The other things to consider here are the
help string which is visible to telemetry consumers, the unit, and the path in the
telemetry tree to the metric. If the return code is zero, then we know that the metric
was successfully initialized and added to the tree and is ready to be used.

Going back to `send_event()` in <a href="/src/engine/drpc_ras.c">drpc_ras.c</a>, we can
now add some code to increment our new counter. Keeping it simple, we can just
increment the counter for every successful event sent. That means putting code like
the following right above the first exit handler:
```C
d_tm_inc_counter(&dss_engine_metrics.drpc_ras_events);
```

And that's it. Recompile and restart your server, and you should now see a counter that
is incremented for every RAS event sent.