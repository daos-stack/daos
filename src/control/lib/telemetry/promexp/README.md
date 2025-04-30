# Prometheus Exporter for DAOS

This package implements the
<a href="https://github.com/prometheus/docs/blob/main/content/docs/instrumenting/exposition_formats.md">Prometheus exposition format</a> 
for DAOS telemetry. It includes a minimal httpd implementation that serves a `/metrics` endpoint which contains
metrics for the `daos_server` or `daos_agent` process hosting the endpoint.

## Client Metrics

Refer to the <a href="/src/client/telemetry.md">Client Telemetry documentation</a> for a full description
of the available options and configuration possibilities.

## Server Metrics

To enable export of server metrics, add the `telemetry_port: <port>` parameter to the `daos_server`
<a href="/utils/config/daos_server.yml">configuration file</a>. The port chosen should not conflict
with other services on the host. The `dmg` tool provided as part of the DAOS admin installation will
default to port 9191 if no port is specified, so this is a good choice if it's available.

## Metric Naming and Labels

The Prometheus exposition format specifies conventions and rules for how metrics are named and
labeled. In the <a href="/src/gurt/telemetry.md">DAOS Telemetry Library</a>, metrics are named
based on their position in the telemetry tree. These names must be flattened and adjusted to
conform to Prometheus standards, however.

For example, the following DAOS metrics track the count of Update (Write) operations
received by targets in a given pool on a single engine (as shown via `daos_metrics --csv`):
```
ID: 0/pool/8d69add3-0e79-4cdd-8663-e32c6ae5fbdb/ops/update/tgt_7,40
ID: 0/pool/8d69add3-0e79-4cdd-8663-e32c6ae5fbdb/ops/update/tgt_0,48
ID: 0/pool/8d69add3-0e79-4cdd-8663-e32c6ae5fbdb/ops/update/tgt_5,36
ID: 0/pool/8d69add3-0e79-4cdd-8663-e32c6ae5fbdb/ops/update/tgt_6,40
ID: 0/pool/8d69add3-0e79-4cdd-8663-e32c6ae5fbdb/ops/update/tgt_4,50
ID: 0/pool/8d69add3-0e79-4cdd-8663-e32c6ae5fbdb/ops/update/tgt_2,54
ID: 0/pool/8d69add3-0e79-4cdd-8663-e32c6ae5fbdb/ops/update/tgt_1,41
ID: 0/pool/8d69add3-0e79-4cdd-8663-e32c6ae5fbdb/ops/update/tgt_3,44
```

The tree is rooted at the engine index (0), and follows a path down the branches to each
`tgt_N` leaf metric node. Each metric has a unique name based on its full path from the root.

This same set of metrics expressed in Prometheus format looks like the
following:
```
- Metric Set: engine_pool_ops_update (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                        Value
    ------  ------                                                        -----
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=0, target=0) 48
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=0, target=1) 41
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=0, target=2) 54
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=0, target=3) 44
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=0, target=4) 50
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=0, target=5) 36
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=0, target=6) 40
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=0, target=7) 40
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=1, target=0) 44
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=1, target=1) 45
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=1, target=2) 39
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=1, target=3) 34
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=1, target=4) 54
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=1, target=5) 35
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=1, target=6) 45
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=1, target=7) 37
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=2, target=0) 39
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=2, target=1) 45
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=2, target=2) 45
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=2, target=3) 34
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=2, target=4) 41
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=2, target=5) 57
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=2, target=6) 46
    Counter (pool=8d69add3-0e79-4cdd-8663-e32c6ae5fbdb, rank=2, target=7) 34
```

This server hosts 3 engines (ranks), hence the increased number of datapoints. Note that
there is now a single metric name: `engine_pool_ops_update`. This name is derived from the path
according to rules defined in the <a href="/src/control/lib/telemetry/promexp/engine.go">engine collector</a>.
The individual datapoints are _labeled_ as characteristics that may be used to refine queries of
the entity being measured (in this case, the number of update operations seen across all pools,
ranks, and targets).

## Basic Prometheus and Grafana Configuration

As the technologies related to metrics collection and analysis are constantly changing and
evolving, an attempt to document a complete solution for DAOS monitoring would be outdated
almost as soon as that document is saved. This section aims to provide a starting point
for readers who have minimal experience with metrics monitoring infrastructure.

Each invocation of `daos_metrics` or `dmg telemetry ...` yields a set of metric
datapoints for a single point in time. This may be useful for simple spot checks
or development tasks, but does not provide much in the way of insights about how
the system is performing _over time_. For that capability, we need something to
periodically collect those datapoints and store them in a way that datapoints
may be compared to each other across time. The technology category for this task
is generally referred to as a Time Series Database (TSDB), and conceptually can be thought
of as a columnar database, where points in time are the columns, and the measurements
taken at those times are stored in the rows.

### Install A Prometheus Server

One of the first examples of a modern Open Source TSDB was <a href="https://prometheus.io/">Prometheus</a>.
We'll use it in this example because it's mature and easy to set up. Follow the
<a href="https://prometheus.io/docs/prometheus/latest/getting_started/">Getting Started</a> guide
to install a Prometheus server. Once you have that running, edit the configuration file
to add a scrape configuration for your DAOS servers:

```
scrape_configs:
- job_name: daos
  scrape_interval: 30s
  static_configs:
  - targets:
    - serverA:9191
    - serverB:9191
    - serverC:9191
```

Then (re)start your Prometheus server to verify that it is collecting metrics. The Prometheus web UI
provides basic querying and graphing capabilities, but should normally be paired with a dashboard
system (e.g. Grafana) to provide a more complete monitoring solution.

Refer to the
<a href="https://prometheus.io/docs/prometheus/latest/configuration/configuration/">Prometheus documentation</a>
for more details on advanced configuration directives.

### Set Up Grafana

Refer to the <a href="/utils/grafana/README.md">README</a> for the DAOS Grafana dashboards.