# Developer Notes for daos_admin

This page is intended to provide developer-focused documentation for the daos_admin helper. For installation instructions, please see the [DAOS Admin Guide](https://daos-stack.github.io/admin/predeployment_check/#elevated-privileges).

## Overview

The DAOS Control Plane performs a number of tasks to enable setup and ongoing administration of a DAOS system. For tasks that require elevated privileges (e.g. formatting/mounting SCM, preparing NVMe storage, etc.), the daos_admin helper has been created to consolidate privilege into a single binary that is easy to control and audit. This isolation allows the DAOS Control Plane and I/O Server processes to run as unprivileged users.

In addition to the security isolation provided by daos_admin, its process isolation simplifies interactions with SPDK and DPDK. Normally, when a process interacts with NVMe devices via DPDK, they become claimed by that process and cannot be shared with other processes without setting up [multi-process support](https://doc.dpdk.org/guides/prog_guide/multi_proc_support.html). As the daos_admin process is ephemeral and only runs long enough to service a request, it cannot make long-term claims on NVMe devices.

## Architecture

Communication between the main Control Plane process (daos_server) and the helper (daos_admin) follows a request-response pattern, with each request resulting in a new invocation of the daos_admin helper as a child process in order to service the request and return a response. Requests and their responses are represented by defined structs that are serialized to and from JSON and sent securely via a custom stdio-based IPC scheme that emulates a bidirectional socket connection. This choice was made to limit the number of potential attack vectors that could be used to introduce unwanted RPCs to be serviced by the privileged helper.

### Providers and Forwarders

The storage management functionality of daos_server is divided by types and areas of responsibility. Each storage type has its own provider (e.g. scm, bdev), which in turn forwards requests to the privileged helper as appropriate. For ease of development and maintenance, daos_server and daos_admin share a common codebase, so that request/response structures and other related code only need to be maintained in one place.

When a request is received by a provider method in daos_server, the request has not yet been forwarded (i.e. its Forwarded member is set to false), and the request is sent to the forwarder implementation defined for that provider. Behind the scenes, a new daos_admin child process is started to receive the request. On the daos_admin side, the request is received and sent to the same provider method as before; the difference is that this time the request is marked as having been forwarded and therefore it is processed locally. The response is sent back to the waiting parent process where any success or error handling occurs. From a caller's perspective, the forwarding to daos_admin is transparent.

## Security

The design of daos_admin was intended to encourage security by default, without requiring complex and error-prone configuration. Notable features:

* Non-persistent privileged process -- only runs long enough to service the requests that cannot be serviced by the main daos_server process
* No open network ports or UNIX sockets -- the only input is via stdin which is owned by the parent daos_server process
* No config files read in daos_admin, further limiting potential for malicious inputs
* When installed from RPM (as is recommended for production installation), daos_admin is setuid root but only executable by root or the daos_admins group (mode 4750), and daos_server is setgid daos_admins

## Examples

(TODO: Add visual aids)