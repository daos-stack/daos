# DAOS Telemetry

DAOS maintains a set of metrics on I/O and internal state. Each DAOS server can
be configured to provide an HTTP endpoint for metrics collection. This
endpoint presents the data in a format compatible with
[Prometheus](https://prometheus.io).

## Configuration

To enable remote telemetry collection, update your DAOS server configuration
file:

```
telemetry_port: 9191
```

The default port number is 9191. Each control plane server will present its
local metrics via the endpoint: `http://<host>:<port>/metrics`

## Collection

There are several different methods available for collecting DAOS metrics.

### DMG

The `dmg` administrative tool provides commands to query an individual DAOS
server for metrics.

The metrics have the same names as seen on the telemetry web endpoint. Only one
DAOS host may be queried at a time.

The output of these commands is available in JSON by using the `-j` option.

#### List Metrics

To list all metrics for the server with descriptions:

```
dmg telemetry [-l <host>] [-p <telemetry-port>] metrics list
```

If no host is provided, the default is localhost. The default port is 9191.

#### Query Metrics

To query the values of one or more metrics on the server:

```
dmg telemetry [-l <host>] [-p <telemetry-port>] metrics query [-m <metric_name>]
```

If no host is provided, the default is localhost. The default port is 9191.

Metric names may be provided in a comma-separated list. If no metric names are
provided, all metrics are queried.

### Prometheus Integration

Prometheus is the preferred way to collect metrics from multiple DAOS servers
at the same time.

To integrate with Prometheus, add a new job to your Prometheus server's
configuration file, with the `targets` set to the hosts and telemetry ports of
your DAOS servers:

```
scrape_configs:
- job_name: daos
  scrape_interval: 5s
  static_configs:
  - targets: ['<host>:<telemetry-port>']
```

If there is not already a Prometheus server set up, DMG offers quick setup
options for DAOS.

To install and configure Prometheus on the local machine:

```
dmg telemetry configure [-i <install-dir>]
```

If no `install-dir` is provided, DMG will attempt to install Prometheus in the
first writable directory found in the user's `PATH`.

To start the Prometheus server on the local machine:

```
dmg telemetry run [-i <install-dir>]
```

If no `install-dir` is provided, DMG will attempt to find Prometheus in the
user's `PATH`.

### daos_metrics

The `daos-server` package includes the `daos_metrics` tool. This tool fetches
metrics from the local host only.

`daos_metrics` displays the metrics in a human-readable tree format or CSV
format (`--csv`).

Each DAOS engine maintains its own metrics.
The `--srv_idx` parameter can be used to specify which engine to query, if there
are multiple engines configured per server.
The default is to query the first engine on the server (index 0).

See `daos_metrics -h` for details on how to filter metrics.
