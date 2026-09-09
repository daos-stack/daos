# Starting a DAOS system

Follow these steps to start a DAOS system that has been started and
formatted before, and is currently stopped on all DAOS servers.

This procedure assumes a standard DAOS RPM installation where
`daos_server` and `daos_agent` are managed by systemd.

All commands except the `daos` command should be run as root.

- Check that the `daos_server` service is stopped on _all_ server nodes
  by running `systemctl status daos_server` in a parallel shell.
  If DAOS server services are still running, determine why they are not
  in the expected stopped state and resolve those issues.

- Start the `daos_server` daemon on all server nodes,
  for example by running `systemctl start daos_server` in a parallel shell.

- Wait for the DAOS control plane and DAOS engines to start on all servers.
  Depending on the number and capacity of the NVMe disks in the servers,
  and the number of targets per engine, this may take a few minutes.
  Progress of the startup can be observed by running `tail -f` on the
  `daos_server.log` logfile. When running with the recommended
  `control_log_mask: DEBUG` the server log should show the following
  among a lot of other debug information:

```
  DEBUG 2026/08/20 14:03:07.660968 logging.go:78: Switching control log level to DEBUG
  DEBUG 2026/08/20 14:03:07.661028 logging.go:91: configured logging: level=DEBUG, file=/var/daos/daos_server.log, json=false
  DEBUG 2026/08/20 14:03:07.750073 fabric.go:1094: fabric interface "ibP1s3" is ready
  DEBUG 2026/08/20 14:03:07.751156 fabric.go:1094: fabric interface "ibs1" is ready
  ...
  DEBUG 2026/08/20 14:03:14.516435 instance_exec.go:37: instance 0: checking if storage is formatted
  DEBUG 2026/08/20 14:03:14.516586 instance_exec.go:37: instance 1: checking if storage is formatted
  ...
  DEBUG 2026/08/20 14:03:15.261004 server_utils.go:686: engine 1: storage ready
  DEBUG 2026/08/20 14:03:15.271039 server_utils.go:686: engine 0: storage ready
  ...
  N0203 INFO 2026/08/20 14:03:15 Starting I/O Engine instance 1: /usr/bin/daos_engine
  N0203 INFO 2026/08/20 14:03:15 Starting I/O Engine instance 0: /usr/bin/daos_engine
  ...
  DEBUG 2026/08/20 14:03:19.002000 server.go:491: requesting immediate GroupUpdate after leader change
  DEBUG 2026/08/20 14:03:19.002013 mgmt_svc.go:399: starting leaderTaskLoop
  DEBUG 2026/08/20 14:03:19.002056 server.go:506: no engines ready for GroupUpdate; waiting 500ms
  DEBUG 2026/08/20 14:03:19.502569 server.go:506: no engines ready for GroupUpdate; waiting 500ms
  DEBUG 2026/08/20 14:03:20.002729 server.go:506: no engines ready for GroupUpdate; waiting 500ms
  <message repeating every 500ms>
```

- As the server log continues to print these "no engines ready" messages,
  it may be useful to run `tail -f` on one of the engine logs to ensure
  that the engine initialization is progressing. After the engine
  initialization completes, the `daos_server.log` should report the
  successful engine start with log entries like this:

```
  N0203 INFO 2026/08/20 14:04:10 daos_engine:1 DAOS I/O Engine (v2.8.0) process 254652 started on rank 1 with 20 target, 10 helper XS, firstcore 0, host N0203.
  N0203 INFO 2026/08/20 14:04:11 daos_engine:0 DAOS I/O Engine (v2.8.0) process 254706 started on rank 0 with 20 target, 10 helper XS, firstcore 0, host N0203.
  ...
  DEBUG 2026/08/20 14:04:11.912940 server.go:413: engines have started
```

- Verify that all engines have started successfully,
  for example using `dmg system query -v` and `dmg pool list` on an admin node.
  Resolve any issues with engines that have not started correctly.

- Start the `daos_agent` daemon on all client nodes,
  for example by running `systemctl start daos_agent` in a parallel shell.

- Verify that DAOS is working as expected on the clients, for example
  by running the `daos system query` and `daos pool list` commands
  in a parallel shell.

!!! note
    The `daos` command is a user command that does not need to be run as root.
    Note that only those pools to which a user has access will be reported by
    the `daos pool list` command. The `root` user has no special privileges
    in DAOS and will also only see pools to which it has access.

- If the DAOS servers' systemd services have been disabled during a preceding
  stop of the DAOS system, those services should eventually be re-enabled
  by running `systemctl enable daos_server` in a parallel shell.

- If the DAOS clients' systemd services have been disabled during a preceding
  stop of the DAOS system, those services should eventually be re-enabled
  by running `systemctl enable daos_agent` in a parallel shell.
