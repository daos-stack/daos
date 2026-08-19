# Starting a DAOS system

Follow these steps to start a DAOS system that is stopped on all DAOS servers.

This procedure assumes a standard DAOS RPM installation where
`daos_server` and `daos_agent` are managed by systemd.

All commands except the `daos` command should be run as root.

- Check that the `daos_server` service is stopped on _all_ server nodes
  by running `systemctl status daos_server` in a parallel shell.
  If DAOS server services are still running, determine why they are not
  in the expected stopped state and resolve those issues.

- Start the `daos_server` daemon on all server nodes,
  for example by running `systemctl start daos_server` in a parallel shell.

- Wait for the DAOS control plane and DAOS engines to start on all servers,
  for example by running `tail -f` on `daos_server.log` and
  the DAOS engine logfiles.
  Depending on the number and capacity of the NVMe disks in the servers,
  and the number of targets per engine, this may take several minutes.

- Verify that all engines have started successfully,
  for example using `dmg system query -v` and `dmg pool list` on an admin node.
  Resolve any issues with engines that have not started correctly.

- Start the `daos_agent` daemon on all client nodes,
  for example by running `systemctl start daos_agent` in a parallel shell.

- Verify that DAOS is working as expected on the clients, for example
  by running the `daos system query` and `daos pool list` commands
  in a parallel shell.
  The `daos` command is a user command that does not need to be run as root.
  Note that only those pools to which a user has access will be reported by
  the `daos pool list` command.

- If the DAOS servers' systemd services have been disabled during a preceding
  stop of the DAOS system, those services should eventually be re-enabled
  by running `systemctl enable daos_server` in a parallel shell.

- If the DAOS clients' systemd services have been disabled during a preceding
  stop of the DAOS system, those services should eventually be re-enabled
  by running `systemctl enable daos_agent` in a parallel shell.
