"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import contextlib
import os
import shutil
import sys
import tempfile
import time

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from exception_utils import CommandFailure
from file_utils import distribute_files
from run_utils import run_remote

# Path to the holder script in the ftest source tree (driver-side). ftest
# installs the security directory next to util/, so the holder lives one
# level up + /security/. The remote path is generated per-test in setUp()
# so concurrent runs on the same host can't clobber each other.
_HOLDER_SOURCE_PATH = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "security",
                 "node_cert_handle_holder.py"))


class NodeAuthTestBase(TestWithServers):
    """Common helpers for per-pool node certificate authentication tests."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.cert_workdir = None
        self.agent_node_cert_dir = None
        self.pool_ca_key = None
        self.pool_ca_cert = None
        self._holder_dir = None
        self._holder_remote_path = None
        self._holder_distributed_hosts = NodeSet()

    def setUp(self):
        super().setUp()
        # dmg pool set-cert / add-client / revoke-client write artifacts to
        # this directory on the test driver host.
        self.cert_workdir = os.path.join(self.test_dir, "pool_certs")
        os.makedirs(self.cert_workdir, exist_ok=True)

        # Where the agent expects per-pool node certs on client hosts.
        sys_name = self.server_managers[0].get_config_value("name") or "daos_server"
        self.agent_node_cert_dir = f"/etc/daos/certs/{sys_name}/node_certs"

        # Per-test temporary directory for the holder script and its
        # pid/ready/log files on remote hosts. mkdtemp() guarantees a
        # unique path that won't collide with concurrent test runs on
        # the same client VM. distribute_files mirrors the local path
        # to remote, so the same path is valid on both sides.
        self._holder_dir = tempfile.mkdtemp(prefix="node_cert_holder_")
        self._holder_remote_path = os.path.join(self._holder_dir, "holder.py")
        shutil.copy(_HOLDER_SOURCE_PATH, self._holder_remote_path)
        self.register_cleanup(self._cleanup_holder_artifacts)

    def _daos_ca_key_path(self):
        """Path to the DAOS CA private key on the test driver.

        gen_certificates.sh writes the CA key to {log_dir}/daosCA/private/daosCA.key
        with 0400 perms; it intentionally lives apart from /etc/daos/certs (which
        only holds public certs distributed to all nodes).
        """
        return os.path.join(self.test_env.log_dir, "daosCA", "private", "daosCA.key")

    def setup_pool_with_cert_auth(self):
        """Create a pool and install a freshly-minted pool CA on it."""
        self.add_pool(connect=False)
        return self.enable_pool_cert_auth(self.pool)

    def enable_pool_cert_auth(self, pool=None):
        """Install a freshly-minted pool CA on `pool` (defaults to self.pool).

        After this returns, the pool requires a per-pool client cert to
        connect — the test driver's default cert is no longer sufficient.
        Registers a cleanup that calls `dmg pool delete-cert --all` on the
        specific pool passed here, so the framework's container/pool
        teardown (which shells out to daos from the driver) can reach the
        pool again.

        The cleanup captures `pool` in its kwargs at registration time
        (matching the add_pool/remove_pool pattern at
        test_utils_pool.py:101) so it remains correct even if `self.pool`
        is later replaced by another `add_pool()` call. LIFO order of
        register_cleanup ensures this runs before any cleanup added
        before this call (e.g. `add_container`) and before the
        framework's auto-destroy of `pool` itself.

        Args:
            pool (TestPool, optional): the pool to enable per-pool cert
                auth on. Defaults to self.pool.

        Returns:
            str: directory containing the freshly-minted CA cert/key files.
        """
        if pool is None:
            pool = self.pool
        # dmg writes files using the canonical lowercase pool UUID; match it
        # everywhere we construct cert/key paths from pool.uuid.
        uuid_lc = str(pool.uuid).lower()
        out_dir = os.path.join(self.cert_workdir, uuid_lc)
        os.makedirs(out_dir, exist_ok=True)
        self.get_dmg_command().pool_set_cert(
            pool=str(pool.uuid),
            daos_ca_key=self._daos_ca_key_path(),
            output=out_dir)
        self.pool_ca_key = os.path.join(out_dir, f"{uuid_lc}_ca.key")
        self.pool_ca_cert = os.path.join(out_dir, f"{uuid_lc}_ca.crt")
        self.register_cleanup(self._disable_pool_cert_auth, pool=pool)
        return out_dir

    def _disable_pool_cert_auth(self, pool):
        """Restore default-CA auth on `pool`. Returns a list of errors
        per the test framework's cleanup protocol."""
        try:
            self.get_dmg_command().pool_delete_cert(
                pool=str(pool.uuid), delete_all=True)
        except CommandFailure as exc:
            return [f"failed to disable per-pool cert auth on {pool.identifier}: {exc}"]
        return []

    def add_client_cert(self, node=None, tenant=None):
        """Mint a client cert via dmg pool add-client. Returns (cert, key)."""
        uuid_lc = str(self.pool.uuid).lower()
        out_dir = os.path.join(self.cert_workdir, "clients")
        os.makedirs(out_dir, exist_ok=True)
        self.get_dmg_command().pool_add_client(
            pool=str(self.pool.uuid),
            pool_ca_key=self.pool_ca_key,
            output=out_dir,
            node=node, tenant=tenant)
        name = node if node else tenant
        return (os.path.join(out_dir, name, f"{uuid_lc}.crt"),
                os.path.join(out_dir, name, f"{uuid_lc}.key"))

    def install_node_cert(self, cert_path, key_path, hosts, pool=None):
        """Distribute a node cert+key to the agent's per-pool cert directory."""
        if pool is None:
            pool = self.pool
        uuid_lc = str(pool.uuid).lower()
        owner = self.agent_managers[0].manager.job.certificate_owner
        # distribute_files mkdir is unprivileged and can't create directories
        # under /etc; pre-create the agent's per-pool cert dir as root.
        mkdir_cmd = (f"sudo mkdir -p {self.agent_node_cert_dir} && "
                     f"sudo chown {owner} {self.agent_node_cert_dir} && "
                     f"sudo chmod 0700 {self.agent_node_cert_dir}")
        result = run_remote(self.log, hosts, mkdir_cmd, timeout=30)
        if not result.passed:
            self.fail(
                f"failed to prepare agent cert dir on {result.failed_hosts}: "
                f"{self._remote_output_blob(result)}")
        dst_cert = os.path.join(self.agent_node_cert_dir, f"{uuid_lc}.crt")
        dst_key = os.path.join(self.agent_node_cert_dir, f"{uuid_lc}.key")
        for src, dst, mode in ((cert_path, dst_cert, "0644"),
                               (key_path, dst_key, "0400")):
            distribute_files(self.log, hosts, src, dst, sudo=True, owner=owner)
            chmod_result = run_remote(self.log, hosts, f"sudo chmod {mode} {dst}",
                                      timeout=30)
            if not chmod_result.passed:
                self.fail(
                    f"failed to chmod {dst} on {chmod_result.failed_hosts}: "
                    f"{self._remote_output_blob(chmod_result)}")

    def remove_node_cert(self, hosts, pool=None):
        """Remove any installed per-pool node cert from the client hosts.
        Best-effort; failures are logged but don't fail the test."""
        if pool is None:
            pool = self.pool
        uuid_lc = str(pool.uuid).lower()
        path = os.path.join(self.agent_node_cert_dir, f"{uuid_lc}.*")
        run_remote(self.log, hosts, f"sudo rm -f {path}", timeout=30)

    def revoke_client(self, node=None, tenant=None,
                      evict_all_handles=False, no_evict=False):
        """Run dmg pool revoke-client; returns the parsed JSON response."""
        out_dir = os.path.join(self.cert_workdir, "revoked")
        os.makedirs(out_dir, exist_ok=True)
        return self.get_dmg_command().pool_revoke_client(
            pool=str(self.pool.uuid),
            pool_ca_key=self.pool_ca_key,
            output=out_dir,
            node=node, tenant=tenant,
            evict_all_handles=evict_all_handles, no_evict=no_evict)

    def _agent_socket_dir(self):
        """Where the agent's dRPC socket lives, as configured for this test."""
        return self.agent_managers[0].get_socket_dir()

    def _remote_output_blob(self, result):
        """Flatten run_remote stdout+stderr into a single string for diagnostics."""
        lines = []
        for data in result.output:
            lines.extend(data.stdout)
            lines.extend(data.stderr)
        return "\n".join(lines)

    def _daos_env_prefix(self):
        """Env-var prefix the remote daos client needs to talk to CART.

        apricot sets D_PROVIDER (and friends) in the test driver's
        os.environ so the local daos client picks the right transport;
        run_remote starts with a fresh shell, so without an explicit
        prefix the remote daos defaults to its compiled-in provider and
        crt_proto_query times out trying to reach all servers.
        DAOS_AGENT_DRPC_DIR points the client at the agent socket apricot
        configured for this run (may be outside /var/run for unprivileged
        agents).
        """
        parts = [f"DAOS_AGENT_DRPC_DIR={self._agent_socket_dir()}"]
        for key in ("D_PROVIDER", "D_INTERFACE", "D_DOMAIN", "D_PORT"):
            val = os.environ.get(key)
            if val:
                parts.append(f"{key}={val}")
        return " ".join(parts)

    def _daos_pool_query_on(self, host):
        """Run `daos -j pool query <uuid>` on `host`; return CommandResult.

        Per-pool node auth is enforced server-side against the connecting
        agent's machine name, so a meaningful pool connect test has to run
        the daos client on the host whose CN matches the installed cert —
        not on the test driver via DaosCommand (which is local-only).
        `self.bin` resolves to the install bin directory (e.g.
        /opt/daos/install/bin for source builds, /usr/bin for RPMs), so
        neither path is hard-coded here.
        """
        cmd = (f"{self._daos_env_prefix()} "
               f"{os.path.join(self.bin, 'daos')} -j pool query {self.pool.uuid}")
        return run_remote(self.log, host, cmd, timeout=60, stderr=True)

    def expect_connect_succeeds(self, host):
        """`daos pool query` on `host` must succeed."""
        result = self._daos_pool_query_on(host)
        if not result.passed:
            self.fail(f"pool connect from {host} expected to succeed; "
                      f"failed_hosts={result.failed_hosts}; "
                      f"output:\n{self._remote_output_blob(result)}")
        return result

    def expect_connect_rejected(self, host, expected_substring=None):
        """`daos pool query` on `host` must fail; optionally assert message."""
        result = self._daos_pool_query_on(host)
        if result.passed:
            self.fail(f"pool connect from {host} expected to be rejected, "
                      f"but it succeeded")
        if expected_substring:
            blob = self._remote_output_blob(result)
            if expected_substring not in blob:
                self.fail(f"connect rejected with unexpected error: {blob}")
        return result

    def _ensure_holder_on(self, host):
        """Copy node_cert_handle_holder.py to the per-test holder dir on
        `host`. Cached against the set of hosts we've already distributed
        to so repeat callers don't re-rsync. distribute_files mirrors the
        local→remote path, so the holder ends up at self._holder_remote_path
        on the remote (same path used to launch and clean up)."""
        new_hosts = NodeSet(str(host)) - self._holder_distributed_hosts
        if not new_hosts:
            return
        distribute_files(self.log, new_hosts, self._holder_remote_path,
                         self._holder_remote_path, sudo=False)
        self._holder_distributed_hosts.add(new_hosts)

    def _cleanup_holder_artifacts(self):
        """Remove the per-test holder directory on every host we
        distributed to and on the test driver. Best-effort: cleanup
        failures are logged but don't fail teardown."""
        if self._holder_distributed_hosts:
            run_remote(self.log, self._holder_distributed_hosts,
                       f"rm -rf {self._holder_dir}", timeout=30)
        shutil.rmtree(self._holder_dir, ignore_errors=True)
        return []

    def _holder_pidfile(self, pool_uuid):
        return os.path.join(self._holder_dir, f"{pool_uuid}.pid")

    def assert_holder_still_alive_on(self, host):
        """Fail unless the holder process started for self.pool is still up.

        The holder's heartbeat loop exits non-zero on the first pool_query
        failure, so a still-running PID is the canary signal that an
        existing handle hasn't been evicted. Used by --no-evict, which
        promises the watermark advance without dropping live handles.
        """
        pidfile = self._holder_pidfile(str(self.pool.uuid))
        check = f"pid=$(cat {pidfile}) && kill -0 $pid"
        result = run_remote(self.log, host, check, timeout=10, stderr=True)
        if not result.passed:
            self.fail(f"pool handle holder on {host} is no longer running "
                      f"(handle was evicted?)")

    @contextlib.contextmanager
    def hold_pool_connection_on(self, host):
        """Open a real pool handle on `host` and keep it live until exit.

        revoke-client's evict_scope/handles_evicted assertions need a handle
        that was opened by an agent on the right machine. The test driver's
        local pydaos can't supply that — its machine name and (typically)
        installed cert don't match the CN under test. So we launch a tiny
        pydaos holder on `host` instead.
        """
        self._ensure_holder_on(host)
        pool_uuid = str(self.pool.uuid)
        # Files are scoped by uuid AND inside the per-test holder dir, so
        # concurrent holders for different pools on the same host don't
        # collide, and concurrent test runs don't collide either.
        pidfile = self._holder_pidfile(pool_uuid)
        readyfile = os.path.join(self._holder_dir, f"{pool_uuid}.ready")
        logfile = os.path.join(self._holder_dir, f"{pool_uuid}.log")
        # Clean up any prior run's leftovers before launching.
        run_remote(self.log, host,
                   f"rm -f {pidfile} {readyfile} {logfile}",
                   timeout=10)

        # Use the same Python interpreter the test driver was launched with
        # — pydaos lives next to it (in the DAOS venv on source builds, in
        # site-packages on RPM installs). Cluster nodes share the install
        # layout, so the path is valid on `host`. The holder needs the
        # DAOS lib64 dir to load libdaos via DaosContext; self.prefix is
        # the install prefix apricot resolved from test_env.daos_prefix.
        # `nohup CMD &` is enough to detach; we deliberately don't use
        # `disown` (a bash builtin) so the command works under any
        # remote login shell. clush passes the command to a shell, so
        # there's no need to wrap it in `bash -c`.
        lib64_dir = os.path.join(self.prefix, "lib64")
        launch = (
            f"{self._daos_env_prefix()} "
            f"nohup {sys.executable} {self._holder_remote_path} {lib64_dir} "
            f"{pool_uuid} {pidfile} {readyfile} "
            f">{logfile} 2>&1 </dev/null &"
        )
        result = run_remote(self.log, host, launch, timeout=30)
        if not result.passed:
            self.fail(f"failed to launch pool handle holder on {host}: "
                      f"{self._remote_output_blob(result)}")

        try:
            # Poll for the ready marker. The holder needs an agent dRPC round
            # trip and a pool connect, both subject to VM scheduling, so give
            # it a generous window.
            ready_deadline = time.time() + 60
            while time.time() < ready_deadline:
                if run_remote(self.log, host, f"test -f {readyfile}",
                              timeout=10, verbose=False).passed:
                    break
                time.sleep(1)
            else:
                run_remote(self.log, host, f"cat {logfile}", timeout=10)
                self.fail(f"pool handle holder on {host} did not become ready")
            yield
        finally:
            # SIGTERM the holder; the script exits without disconnecting so
            # the server-side eviction path can verify on a still-live handle
            # if revoke landed before this. Best-effort cleanup of the
            # marker files.
            run_remote(
                self.log, host,
                f"if [ -s {pidfile} ]; then kill $(cat {pidfile}) 2>/dev/null; "
                f"fi; rm -f {pidfile} {readyfile} {logfile}",
                timeout=15)
