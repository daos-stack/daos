"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from node_auth_test_base import NodeAuthTestBase


class PoolNodeAuthRevokeTest(NodeAuthTestBase):
    """Revocation tests: a revoked client must be evicted and cannot reconnect.

    :avocado: recursive
    """

    def test_revoke_blocks_reconnect(self):
        """After revoke, a fresh connect with the old cert is rejected.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_blocks_reconnect
        """
        self.setup_pool_with_cert_auth()
        host = self.hostlist_clients[0:1]
        cn = str(host[0])
        cert, key = self.add_client_cert(node=cn)
        self.install_node_cert(cert, key, host)
        self.expect_connect_succeeds(host)

        self.revoke_client(node=cn)
        revs = self.get_dmg_command().pool_list_revocations(pool=str(self.pool.uuid))
        if f"node:{cn}" not in revs["response"]["revocations"]:
            self.fail("list-revocations missing the revoked CN")

        # The agent's cached old cert must not authenticate.
        self.expect_connect_rejected(host)

    def test_revoke_replacement_works(self):
        """After revoke, the replacement cert must authenticate.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_replacement_works
        """
        self.setup_pool_with_cert_auth()
        host = self.hostlist_clients[0:1]
        cn = str(host[0])
        cert, key = self.add_client_cert(node=cn)
        self.install_node_cert(cert, key, host)
        self.expect_connect_succeeds(host)

        resp = self.revoke_client(node=cn)["response"]
        # Install the replacement cert returned by revoke-client.
        self.install_node_cert(resp["cert_path"], resp["key_path"], host)
        self.expect_connect_succeeds(host)

    def test_revoke_node_evicts_per_cn(self):
        """Default revoke for node:X evicts only handles whose machine matches X.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_node_evicts_per_cn
        """
        # Create the container BEFORE enabling per-pool cert auth — once
        # auth is enabled, add_container's daos invocation on the driver
        # would hit DER_NO_CERT (driver has no per-pool cert).
        self.add_pool(connect=False)
        self.add_container(self.pool)
        self.enable_pool_cert_auth()
        host = self.hostlist_clients[0:1]
        cn = str(host[0])
        cert, key = self.add_client_cert(node=cn)
        self.install_node_cert(cert, key, host)
        # Plant a live pool handle whose machine name matches the CN we're
        # about to revoke. The holder runs pydaos on `host` so server-side
        # eviction has a real, machine-bound handle to count.
        with self.hold_pool_connection_on(host):
            resp = self.revoke_client(node=cn)["response"]
            if resp.get("evict_scope") != "machine":
                self.fail(f"expected evict_scope=machine, got {resp.get('evict_scope')}")
            if resp.get("handles_evicted", 0) <= 0:
                self.fail("expected handles_evicted > 0, got 0")

            self.expect_connect_rejected(host)

        # Replacement must work after the holder is torn down.
        self.install_node_cert(resp["cert_path"], resp["key_path"], host)
        self.expect_connect_succeeds(host)

    def test_revoke_unknown_node_zero_count(self):
        """Revoking a non-existent node yields zero evict count (typo signal).

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_unknown_node_zero_count
        """
        self.setup_pool_with_cert_auth()
        resp = self.revoke_client(node="nonexistent-host-name")["response"]
        if resp.get("evict_scope") != "machine":
            self.fail(f"expected evict_scope=machine, got {resp.get('evict_scope')}")
        if resp.get("handles_evicted", -1) != 0:
            self.fail(f"expected handles_evicted=0, got {resp.get('handles_evicted')}")

    def test_revoke_no_evict_keeps_handle(self):
        """--no-evict advances the watermark without dropping live handles.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_no_evict_keeps_handle
        """
        # See test_revoke_node_evicts_per_cn for why we don't use
        # setup_pool_with_cert_auth here: add_container must run BEFORE
        # per-pool cert auth is enabled on the pool.
        self.add_pool(connect=False)
        self.add_container(self.pool)
        self.enable_pool_cert_auth()
        host = self.hostlist_clients[0:1]
        cn = str(host[0])
        cert, key = self.add_client_cert(node=cn)
        self.install_node_cert(cert, key, host)

        # Plant a live, machine-bound handle so --no-evict has something to
        # preserve. The holder runs a heartbeat pool_query against its own
        # handle and exits non-zero on the first failure; "process still
        # alive after revoke" is the canary signal that the existing
        # handle was not torn down.
        with self.hold_pool_connection_on(host):
            resp = self.revoke_client(node=cn, no_evict=True)["response"]
            if resp.get("evict_scope") != "none":
                self.fail(f"expected evict_scope=none, got {resp.get('evict_scope')}")
            if resp.get("handles_evicted", -1) != 0:
                self.fail(f"expected handles_evicted=0, got {resp.get('handles_evicted')}")

            # Give the heartbeat loop a couple of cycles to either keep
            # going or trip on a server-side eviction.
            time.sleep(5)
            self.assert_holder_still_alive_on(host)

            # Watermark advanced: a fresh connect from the same CN is blocked
            # even though the existing handle was left alone.
            self.expect_connect_rejected(host)

        # Install the replacement cert so framework teardown (which shells
        # out to `daos container destroy`) can reconnect to the pool;
        # otherwise cleanup hits DER_BAD_CERT and the test is marked
        # TestSetupFail.
        self.install_node_cert(resp["cert_path"], resp["key_path"], host)

    def test_tenant_revocation_blocks_all_holders(self):
        """Revoking a tenant CN must reject every host using that cert.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_tenant_revocation_blocks_all_holders
        """
        self.setup_pool_with_cert_auth()
        if len(self.hostlist_clients) < 2:
            self.fail("test requires at least 2 client hosts")
        h1, h2 = self.hostlist_clients[0:1], self.hostlist_clients[1:2]

        cert, key = self.add_client_cert(tenant="teamA")
        self.install_node_cert(cert, key, h1)
        self.install_node_cert(cert, key, h2)
        # Connect succeeds from either host.
        self.expect_connect_succeeds(h1)
        self.expect_connect_succeeds(h2)

        resp = self.revoke_client(tenant="teamA")["response"]
        # Tenant revocation has no per-CN selectivity, so default mode evicts
        # the whole pool.
        if resp.get("evict_scope") != "pool":
            self.fail(f"expected evict_scope=pool, got {resp.get('evict_scope')}")
        # Every host using the tenant cert must be rejected.
        self.expect_connect_rejected(h1)
        self.expect_connect_rejected(h2)
