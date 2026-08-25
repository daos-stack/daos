"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from exception_utils import CommandFailure
from file_utils import change_file_mode, change_file_owner, create_directory, distribute_files
from general_utils import get_log_file
from host_utils import get_local_host
from pydaos.raw import DaosApiError
from run_utils import run_remote


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

    def setUp(self):
        super().setUp()
        # dmg node-auth enable/issue write here (driver host).
        self.cert_workdir = os.path.join(self.test_dir, "pool_certs")
        os.makedirs(self.cert_workdir, exist_ok=True)

        # Where the agent expects per-pool node certs on client hosts.
        self.agent_node_cert_dir = "/etc/daos/certs/node_certs"

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

    def enable_pool_cert_auth(self, pool=None):
        """Install a pool CA on `pool` and register a cleanup that disables it.

        Args:
            pool (TestPool, optional): the pool to enable per-pool cert
                auth on. Defaults to self.pool.

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
            output=out_dir)
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

    def generate_client_cert(self, node=None, tenant=None, ca_key=None):
        """Mint a client cert via generate-cert, as on a signing host with no
        system connection. Signs with the pool CA key unless ca_key is given.
        Returns (cert, key)."""
        out_dir = os.path.join(self.cert_workdir, "offline_clients")
        self.get_dmg_command().pool_node_auth_generate_cert(
            pool=str(self.pool.uuid),
            pool_ca_key=ca_key or self.pool_ca_key,
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

    def revoke_client(self, node=None, tenant=None):
        """Run dmg pool node-auth revoke; returns the parsed JSON response.

        Revocation needs no CA key and mints nothing; reissue via
        issue_client_cert() if the identity should regain access.
        """
        return self.get_dmg_command().pool_node_auth_revoke(
            pool=str(self.pool.uuid), node=node, tenant=tenant)

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

    def _daos_pool_query_on(self, host):
        """Run `daos -j pool query <uuid>` on `host`; return CommandResult."""
        daos = self.get_daos_command()
        daos.env["DAOS_AGENT_DRPC_DIR"] = self.agent_managers[0].get_socket_dir()
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

    def expect_connect_rejected(self, host, error):
        """`daos pool query` on `host` must fail with `error` in its output."""
        result = self._daos_pool_query_on(host)
        if result.passed:
            self.fail(f"pool connect from {host} expected to be rejected, "
                      f"but it succeeded")
        blob = f"{result.joined_stdout}\n{result.joined_stderr}"
        if error not in blob:
            self.fail(f"connect from {host} rejected without {error}: {blob}")
        return result

    @property
    def driver_host(self):
        """The test driver, which runs its own agent: a pool handle held
        in-process is a real node-authenticated handle for this machine."""
        return get_local_host()

    def install_driver_cert(self):
        """Issue the driver its own node cert and deploy it to the driver's agent."""
        cert, key = self.issue_client_cert(node=str(self.driver_host))
        self.install_node_cert(cert, key, self.driver_host)

    def assert_pool_handle_evicted(self):
        """Fail unless the driver's open handle on self.pool has been evicted."""
        try:
            self.pool.pool.pool_query()
        except DaosApiError:
            # Nothing left to disconnect at teardown.
            self.pool.connected = False
            return
        self.fail(f"handle on {self.pool.identifier} still works after eviction")
