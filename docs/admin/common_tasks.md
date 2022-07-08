# DAOS Common Tasks

This section describes some of the common tasks handled by admins at a high level. See [System Deployment](./deployment.md#system-deployment), [DAOS System Administration](./administration.md#daos-system-administration), and [Pool Operations](./pool_operations.md#pool-operations) for more detailed explanations about each step.

## Single host setup with PMEM and NVMe

1. Check PMEM and NVMe are discovered by the system. Format and reset them.
2. Check network configuration. Check that `ib` interfaces are active.
3. Install `daos-server` and `daos-client` RPMs.
4. Generate certificate files.
5. Copy one of the example configs from `utils/config/examples` to
`/etc/daos` and adjust it based on the environment. E.g., `access_points`,
`bdev_class`.
6. Start `daos_server`.
7. Use `dmg config generate` to generate the config file that contains PMEM and
NVMe.
8. Define the certificate files in the server config.
9. Start server with the generated config file.
10. Check that it's waiting for SCM format. Call dmg storage format.
11. Create a small pool; ~500MB.
12. Define the certificate files in the agent config.
13. Start agent.
14. Create a POSIX container with daos command.
15. Mount the container with dfuse.
16. Add a large file that's less than the 500MB pool size into the container.
17. Call `dmg pool query` and check that the free size has declined.

## Multiple host setup with PMEM and NVMe

1. Check PMEM and NVMe configurations are homogeneous. I.e., same number of
disks, size, address, etc.
2. Check network configuration. Check that both the server and the client hosts
can communicate with the network interface.
3. Install the same version of `daos-server` and `daos-client` RPMs to all the
hosts.
4. Generate certificate files and distribute them to all the hosts.
5. Copy one of the example configs from `utils/config/examples` to
`/etc/daos` of one of the server hosts and adjust it based on the environment.
E.g., `access_points`, `bdev_class`.
6. Start `daos_server`.
7. Use dmg config generate to generate the config file that contains PMEM and
NVMe.
8. Distribute the config file to `/etc/daos` of all hosts.
9. Start server on all the hosts.
10. Check that it's waiting for SCM format. Call `dmg storage format` against all
server hosts.
11. Check that the servers are running on all the hosts with `dmg system query
--verbose`.
12. Create a small pool; ~500MB.
13. Define the certificate files in the agent config in the client host.
14. Start agent.
15. Create a POSIX container with daos command.
16. Mount the container with dfuse.
17. Add a large file that's less than the 500MB pool size into the container.
18. Call dmg pool query and check that the free size has declined.

## Pool size management

1. Start DAOS server with PMEM + NVMe and format.
2. Create a pool with the size percentage. For example,
```
dmg pool create --size=50%
```
The percentage is applied to the usable space.

## Run dmg remotely

1. Start DAOS server on one host.
2. Create a file that specifies the server host in `/etc/daos`. It's usually
called `daos_control.yml`. Add the following:
```
hostlist:
- <server_host>
name: <group_name>
port: 10001
transport_config:
  allow_insecure: false
  ca_cert: /etc/daos/certs/daosCA.crt
  cert: /etc/daos/certs/admin.crt
  key: /etc/daos/certs/admin.key
```
`server_host` is the hostname where the server is running. `group_name` is
usually `daos_server`. Also, adjust port and `transport_config` accordingly.

3. `dmg` should be able to talk to the server.

## Server config technique

- Use the network interface, PMEM, and NVMe from the same NUMA Socket ID for
the best performance.
- Call `dmg storage scan --verbose` to see the list of devices and their Socket
ID.
- Call `dmg network scan -p all` to see the provider, interface, and their
associated Socket ID.
- Use `control_log_mask: ERROR` to show only necessary messages in status.

## Multiple server ranks in a single host

- Specify multiple engines under `engines` section. Each section starts with a
dash `-`.
- Each engine must use different `fabric_iface_port`. The port numbers should
differ by at least 100.
- Each engine must have different `log_file`.
- Use different `fabric_iface` for the best performance.
- Each engine must have unique `scm_mount`, `scm_list`, and `bdev_list`.
