# Stopping a DAOS system

Follow these steps to completely stop a running DAOS system.

This procedure assumes a standard DAOS RPM installation where
`daos_server` and `daos_agent` are managed by systemd.

All commands should be run as root.

- Check the DAOS system health. At a minimum, run the
  `dmg system query -v` and `dmg pool list` commands on an admin node
  and check that the reported status is as expected.
  If any issues are found, it is advisable to address those issues
  before stopping the whole DAOS system.

- Ensure that there are no client applications with open pool connections.
  In paticluar, terminate any user jobs that are using DAOS
  and terminate any `dfuse` mounts that may still exist.
  If necessary, the `dmg pool evict` command can be used on an admin node
  to disconnect any active pool connections.

- Stop the `daos_agent` daemon on _all_ client nodes
  by running `systemctl stop daos_agent` in a parallel shell.

- Check that the `daos_agent` service is stopped on _all_ client nodes
  by running `systemctl status daos_agent` in a parallel shell.
  Resolve any issues when DAOS agent services are still running.

- Stop the DAOS engines by running `dmg system stop` on _one_ admin node.

- Verify that all engines are stopped by running the
  `dmg system query -v` command on an admin node.
  If any engines are not in a "Stopped" state, address those issues.

- Stop the `daos_server` daemon on _all_ server nodes
  by running `systemctl stop daos_server` in a parallel shell.

- Check that the `daos_server` service is stopped on _all_ server nodes
  by running `systemctl status daos_server` in a parallel shell.
  Resolve any issues when DAOS server services are still running.

- In scenarios where an automatic restart of the `daos_server` daemon
  after a server reboot is undesirable (for example, during a maintenance
  where node status needs to be checked before restarting DAOS),
  disable the systemd service on the affected servers by running
  `systemctl disable daos_server` in a parallel shell.

- Similarly, to prevent an automatic restart of the `daos_agent` daemon
   after client reboots, disable the systemd service on the affected clients
  by running `systemctl disable daos_agent` in a parallel shell.
