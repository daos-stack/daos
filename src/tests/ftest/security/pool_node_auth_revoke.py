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
        cert, key = self.issue_client_cert(node=cn)
        self.install_node_cert(cert, key, host)
        self.expect_connect_succeeds(host)

        self.revoke_client(node=cn)
        status = self.get_dmg_command().pool_node_auth_status(pool=str(self.pool.uuid))
        if f"node:{cn}" not in status["response"]["revocations"]:
            self.fail("status missing the revoked CN in revocations")

        # The agent's cached old cert must not authenticate.
        self.expect_connect_rejected(host)

    def test_revoke_reissue_works(self):
        """After revoke, a reissued cert must authenticate.

        issue postdates the new cert past the committed watermark; a cert
        minted after revocation must never be dead on arrival.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_reissue_works
        """
        self.setup_pool_with_cert_auth()
        host = self.hostlist_clients[0:1]
        cn = str(host[0])
        cert, key = self.issue_client_cert(node=cn)
        self.install_node_cert(cert, key, host)
        self.expect_connect_succeeds(host)

        self.revoke_client(node=cn)
        # Reissue for the same identity and install the fresh cert.
        cert, key = self.issue_client_cert(node=cn)
        self.install_node_cert(cert, key, host)
        self.expect_connect_succeeds(host)

    def test_revoke_node_evicts_per_cn(self):
        """Default revoke for node:X evicts only handles whose machine matches X.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_node_evicts_per_cn
        """
        # Create the container before enabling auth: the driver has no node cert.
        self.pool = self.get_pool(connect=False)
        self.container = self.get_container(self.pool)
        self.enable_pool_cert_auth()
        host = self.hostlist_clients[0:1]
        cn = str(host[0])
        cert, key = self.issue_client_cert(node=cn)
        self.install_node_cert(cert, key, host)
        # A live machine-bound handle gives revoke something to evict.
        with self.hold_pool_connection_on(host):
            resp = self.revoke_client(node=cn)["response"]
            if resp["evict_scope"] != "machine":
                self.fail(f"expected evict_scope=machine, got {resp['evict_scope']}")
            if resp["handles_evicted"] <= 0:
                self.fail("expected handles_evicted > 0, got 0")

            self.expect_connect_rejected(host)

        # A reissued cert must work after the holder is torn down.
        cert, key = self.issue_client_cert(node=cn)
        self.install_node_cert(cert, key, host)
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
        if resp["evict_scope"] != "machine":
            self.fail(f"expected evict_scope=machine, got {resp['evict_scope']}")
        if resp["handles_evicted"] != 0:
            self.fail(f"expected handles_evicted=0, got {resp['handles_evicted']}")

    def test_revoke_no_evict_keeps_handle(self):
        """--no-evict advances the watermark without dropping live handles.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_no_evict_keeps_handle
        """
        # Create the container before enabling auth: the driver has no node cert.
        self.pool = self.get_pool(connect=False)
        self.container = self.get_container(self.pool)
        self.enable_pool_cert_auth()
        host = self.hostlist_clients[0:1]
        cn = str(host[0])
        cert, key = self.issue_client_cert(node=cn)
        self.install_node_cert(cert, key, host)

        # The holder's heartbeat exits on eviction; still alive means the handle survived.
        with self.hold_pool_connection_on(host):
            resp = self.revoke_client(node=cn, no_evict=True)["response"]
            if resp["evict_scope"] != "none":
                self.fail(f"expected evict_scope=none, got {resp['evict_scope']}")
            if resp["handles_evicted"] != 0:
                self.fail(f"expected handles_evicted=0, got {resp['handles_evicted']}")

            # Allow a couple of heartbeat cycles.
            time.sleep(5)
            self.assert_holder_still_alive_on(host)

            # A fresh connect from the same CN is blocked.
            self.expect_connect_rejected(host)

        # Reissue so framework teardown can reconnect.
        cert, key = self.issue_client_cert(node=cn)
        self.install_node_cert(cert, key, host)

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

        cert, key = self.issue_client_cert(tenant="teamA")
        self.install_node_cert(cert, key, h1)
        self.install_node_cert(cert, key, h2)
        # Connect succeeds from either host.
        self.expect_connect_succeeds(h1)
        self.expect_connect_succeeds(h2)

        resp = self.revoke_client(tenant="teamA")["response"]
        # Tenant revocation evicts pool-wide.
        if resp["evict_scope"] != "pool":
            self.fail(f"expected evict_scope=pool, got {resp['evict_scope']}")
        # Every host using the tenant cert must be rejected.
        self.expect_connect_rejected(h1)
        self.expect_connect_rejected(h2)
