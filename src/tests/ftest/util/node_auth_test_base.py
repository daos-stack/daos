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
from file_utils import change_file_mode, change_file_owner, create_directory, distribute_files
from general_utils import get_log_file
from run_utils import run_remote

# Holder script source; setUp() makes a per-test copy.
_HOLDER_SOURCE_PATH = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "security",
                 "node_cert_handle_holder.py"))


def pool_ca_paths(directory, pool_uuid):
    """Return the (cert, key) paths of a pool CA in directory, as dmg names them."""
    return (os.path.join(directory, f"{pool_uuid}_ca.crt"),
            os.path.join(directory, f"{pool_uuid}_ca.key"))


def node_cert_paths(directory, pool_uuid):
    """Return the (cert, key) paths of a node certificate in directory, as the agent names them."""
    return (os.path.join(directory, f"{pool_uuid}.crt"),
            os.path.join(directory, f"{pool_uuid}.key"))


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
        # dmg node-auth enable/issue write here (driver host).
        self.cert_workdir = os.path.join(self.test_dir, "pool_certs")
        os.makedirs(self.cert_workdir, exist_ok=True)

        # Where the agent expects per-pool node certs on client hosts.
        self.agent_node_cert_dir = "/etc/daos/certs/node_certs"

        # Per-test holder dir; the same path is used locally and remotely.
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
        return get_log_file("daosCA/private/daosCA.key")

    def generate_pool_ca(self, name, pool=None):
        """Mint a pool CA with generate-ca, as an admin holding the DAOS CA key
        would on a host without a system connection. Returns (cert, key)."""
        if pool is None:
            pool = self.pool
        out_dir = os.path.join(self.cert_workdir, name)
        self.get_dmg_command().pool_node_auth_generate_ca(
            pool=str(pool.uuid),
            daos_ca_cert=get_log_file("daosCA/certs/daosCA.crt"),
            daos_ca_key=self._daos_ca_key_path(),
            output=out_dir)
        return pool_ca_paths(out_dir, str(pool.uuid).lower())

    def setup_pool_with_cert_auth(self):
        """Create a pool and install a freshly-minted pool CA on it."""
        self.pool = self.get_pool(connect=False)
        return self.enable_pool_cert_auth(self.pool)

    def enable_pool_cert_auth(self, pool=None, no_evict=False):
        """Install a pool CA on `pool` and register a cleanup that disables it.

        Args:
            pool (TestPool, optional): the pool to enable per-pool cert
                auth on. Defaults to self.pool.
            no_evict (bool, optional): keep existing handles open.

        Returns:
            dict: the dmg enable response.
        """
        if pool is None:
            pool = self.pool
        # dmg names files with the lowercase pool UUID.
        uuid_lc = str(pool.uuid).lower()
        out_dir = os.path.join(self.cert_workdir, uuid_lc)
        os.makedirs(out_dir, exist_ok=True)
        resp = self.get_dmg_command().pool_node_auth_enable(
            pool=str(pool.uuid),
            daos_ca_key=self._daos_ca_key_path(),
            output=out_dir, no_evict=no_evict)
        self.pool_ca_cert, self.pool_ca_key = pool_ca_paths(out_dir, uuid_lc)
        self.register_cleanup(self._disable_pool_cert_auth, pool=pool)
        return resp

    def _disable_pool_cert_auth(self, pool):
        """Restore default-CA auth on `pool`. Returns a list of errors
        per the test framework's cleanup protocol."""
        try:
            self.get_dmg_command().pool_node_auth_disable(pool=str(pool.uuid))
        except CommandFailure as exc:
            return [f"failed to disable per-pool cert auth on {pool.identifier}: {exc}"]
        return []

    def generate_client_cert(self, node=None, tenant=None):
        """Mint a client cert via generate-cert, as on a signing host with no
        system connection. Returns (cert, key)."""
        out_dir = os.path.join(self.cert_workdir, "offline_clients")
        self.get_dmg_command().pool_node_auth_generate_cert(
            pool=str(self.pool.uuid),
            pool_ca_key=self.pool_ca_key,
            output=out_dir,
            node=node, tenant=tenant)
        name = node if node else tenant
        return node_cert_paths(os.path.join(out_dir, name), str(self.pool.uuid).lower())

    def issue_client_cert(self, node=None, tenant=None):
        """Mint a client cert via dmg pool node-auth issue. Returns (cert, key).

        issue postdates the cert past any revocation watermark for the
        identity, so this is also the reissue path after revoke.
        """
        uuid_lc = str(self.pool.uuid).lower()
        out_dir = os.path.join(self.cert_workdir, "clients")
        os.makedirs(out_dir, exist_ok=True)
        self.get_dmg_command().pool_node_auth_issue(
            pool=str(self.pool.uuid),
            pool_ca_key=self.pool_ca_key,
            output=out_dir,
            node=node, tenant=tenant)
        name = node if node else tenant
        return node_cert_paths(os.path.join(out_dir, name), uuid_lc)

    def install_node_cert(self, cert_path, key_path, hosts, pool=None):
        """Distribute a node cert+key to the agent's per-pool cert directory."""
        if pool is None:
            pool = self.pool
        uuid_lc = str(pool.uuid).lower()
        owner = self.agent_managers[0].manager.job.certificate_owner
        cert_dir = self.agent_node_cert_dir
        for result in (create_directory(self.log, hosts, cert_dir, user="root"),
                       change_file_owner(self.log, hosts, cert_dir, owner, owner, user="root"),
                       change_file_mode(self.log, hosts, cert_dir, "0700", user="root")):
            if not result.passed:
                self.fail(f"failed to prepare {cert_dir} on {result.failed_hosts}: "
                          f"{result.joined_stderr}")
        dst_cert, dst_key = node_cert_paths(cert_dir, uuid_lc)
        for src, dst, mode in ((cert_path, dst_cert, "0644"), (key_path, dst_key, "0400")):
            distribute_files(self.log, hosts, src, dst, sudo=True, owner=owner)
            result = change_file_mode(self.log, hosts, dst, mode, user="root")
            if not result.passed:
                self.fail(f"failed to chmod {dst} on {result.failed_hosts}: "
                          f"{result.joined_stderr}")

    def remove_node_cert(self, hosts, pool=None):
        """Remove any installed per-pool node cert from the client hosts (best-effort)."""
        if pool is None:
            pool = self.pool
        uuid_lc = str(pool.uuid).lower()
        path = os.path.join(self.agent_node_cert_dir, f"{uuid_lc}.*")
        run_remote(self.log, hosts, f"sudo rm -f {path}", timeout=30)

    def revoke_client(self, node=None, tenant=None,
                      evict_all_handles=False, no_evict=False):
        """Run dmg pool node-auth revoke; returns the parsed JSON response.

        Revocation needs no CA key and mints nothing; reissue via
        issue_client_cert() if the identity should regain access.
        """
        return self.get_dmg_command().pool_node_auth_revoke(
            pool=str(self.pool.uuid),
            node=node, tenant=tenant,
            evict_all_handles=evict_all_handles, no_evict=no_evict)

    def check_node_cert_on(self, host, expect_pass=True, pool=None):
        """Run daos_agent check-node-cert on `host` and assert the verdict.

        Exercises the preflight end-to-end: live agent config, deployed
        cert files, real machine-name comparison — the same command an
        admin runs to verify a deployment. Exit code carries the verdict.
        """
        if pool is None:
            pool = self.pool
        result = self.agent_managers[0].check_node_cert(str(pool.uuid).lower(), hosts=host)
        if expect_pass and not result.passed:
            self.fail(f"check-node-cert on {host} expected to pass: "
                      f"{result.joined_stdout}\n{result.joined_stderr}")
        if not expect_pass and result.passed:
            self.fail(f"check-node-cert on {host} expected to fail but passed")
        return result

    def _agent_socket_dir(self):
        """Where the agent's dRPC socket lives, as configured for this test."""
        return self.agent_managers[0].get_socket_dir()

    def _daos_pool_query_on(self, host):
        """Run `daos -j pool query <uuid>` on `host`; return CommandResult."""
        daos = self.get_daos_command()
        daos.env["DAOS_AGENT_DRPC_DIR"] = self._agent_socket_dir()
        daos.json.update(True)
        daos.set_command(("pool", "query"), pool=str(self.pool.uuid))
        return run_remote(self.log, host, daos.with_exports, timeout=60, stderr=True)

    def expect_connect_succeeds(self, host):
        """`daos pool query` on `host` must succeed."""
        result = self._daos_pool_query_on(host)
        if not result.passed:
            self.fail(f"pool connect from {host} expected to succeed; "
                      f"failed_hosts={result.failed_hosts}; "
                      f"output:\n{result.joined_stdout}\n{result.joined_stderr}")
        return result

    def expect_connect_rejected(self, host, expected_substring=None):
        """`daos pool query` on `host` must fail; optionally assert message."""
        result = self._daos_pool_query_on(host)
        if result.passed:
            self.fail(f"pool connect from {host} expected to be rejected, "
                      f"but it succeeded")
        if expected_substring:
            blob = f"{result.joined_stdout}\n{result.joined_stderr}"
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
        """Remove the per-test holder directory everywhere (best-effort)."""
        if self._holder_distributed_hosts:
            run_remote(self.log, self._holder_distributed_hosts,
                       f"rm -rf {self._holder_dir}", timeout=30)
        shutil.rmtree(self._holder_dir, ignore_errors=True)
        return []

    def _holder_pidfile(self, pool_uuid):
        return os.path.join(self._holder_dir, f"{pool_uuid}.pid")

    def assert_holder_exited_on(self, host, timeout=30):
        """Fail unless the holder process for self.pool exits within timeout;
        its heartbeat exits on the first failed pool query after eviction."""
        pidfile = self._holder_pidfile(str(self.pool.uuid))
        check = f"pid=$(cat {pidfile}) && kill -0 $pid"
        deadline = time.time() + timeout
        while time.time() < deadline:
            if not run_remote(self.log, host, check, timeout=10, verbose=False).passed:
                return
            time.sleep(1)
        self.fail(f"pool handle holder on {host} is still running {timeout}s after enable")

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
        """Open a real pool handle on `host` (via its agent) and keep it live until exit."""
        self._ensure_holder_on(host)
        pool_uuid = str(self.pool.uuid)
        pidfile = self._holder_pidfile(pool_uuid)
        readyfile = os.path.join(self._holder_dir, f"{pool_uuid}.ready")
        logfile = os.path.join(self._holder_dir, f"{pool_uuid}.log")
        # Clean up any prior run's leftovers before launching.
        run_remote(self.log, host,
                   f"rm -f {pidfile} {readyfile} {logfile}",
                   timeout=10)

        # sys.executable has pydaos; lib64 is where the holder finds libdaos.
        lib64_dir = os.path.join(self.prefix, "lib64")
        launch = (
            f"DAOS_AGENT_DRPC_DIR={self._agent_socket_dir()} "
            f"nohup {sys.executable} {self._holder_remote_path} {lib64_dir} "
            f"{pool_uuid} {pidfile} {readyfile} "
            f">{logfile} 2>&1 </dev/null &"
        )
        result = run_remote(self.log, host, launch, timeout=30)
        if not result.passed:
            self.fail(f"failed to launch pool handle holder on {host}: "
                      f"{result.joined_stdout}\n{result.joined_stderr}")

        try:
            # Ready needs an agent round trip and a pool connect.
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
            # SIGTERM only; the holder exits without disconnecting.
            run_remote(
                self.log, host,
                f"if [ -s {pidfile} ]; then kill $(cat {pidfile}) 2>/dev/null; "
                f"fi; rm -f {pidfile} {readyfile} {logfile}",
                timeout=15)
