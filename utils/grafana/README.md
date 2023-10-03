# Grafana Dashboards

This directory contains JSON-formatted dashboards intended to be imported into
[Grafana](https://grafana.com) for viewing a subset of DAOS metrics in a
graphical format.

## Setup Instructions

1. Ensure the `telemetry_port` is set in the config file for every server in
   your cluster before starting your daos_server(s).
2. Install and configure [Prometheus](https://prometheus.io/) to collect metrics
   from your DAOS servers. You may use `dmg telemetry config -i <install-dir>` as a shortcut.
3. Start Prometheus.
4. Install Grafana according to their installation instructions and start the
   Grafana service: `sudo systemctl start grafana-server`
5. On Grafana, create a data source called "Prometheus" configured to point at
   your Prometheus instance.
6. Import the JSON file(s) into Grafana.
