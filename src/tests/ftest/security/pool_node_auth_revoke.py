"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from node_auth_test_base import NodeAuthTestBase


class PoolNodeAuthRevokeTest(NodeAuthTestBase):
    """Revocation tests: a revoked client is evicted, cannot reconnect, and can be reissued.

    :avocado: recursive
    """

    def test_revoke_and_reissue(self):
        """revoke blocks the old cert immediately; a reissued cert works at once.

        issue postdates the new cert past the committed watermark; a cert
        minted after revocation must never be dead on arrival.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_and_reissue
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
        self.expect_connect_rejected(host, "DER_BAD_CERT")

        cert, key = self.issue_client_cert(node=cn)
        self.install_node_cert(cert, key, host)
        self.expect_connect_succeeds(host)

    def test_revoke_node_evicts_per_cn(self):
        """Default revoke for node:X evicts the live handles whose machine is X.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthRevokeTest,test_revoke_node_evicts_per_cn
        """
        self.setup_pool_with_cert_auth()
        self.install_driver_cert()
        self.pool.connect()

        resp = self.revoke_client(node=str(self.driver_host))["response"]
        if resp["evict_scope"] != "machine":
            self.fail(f"expected evict_scope=machine, got {resp['evict_scope']}")
        if resp["handles_evicted"] < 1:
            self.fail(f"expected handles_evicted >= 1, got {resp['handles_evicted']}")
        self.assert_pool_handle_evicted()

    def test_tenant_revocation_blocks_all_holders(self):
        """Revoking a tenant evicts pool-wide and rejects every host using that cert.

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
        for host in (h1, h2, self.driver_host):
            self.install_node_cert(cert, key, host)
        self.expect_connect_succeeds(h1)
        self.expect_connect_succeeds(h2)
        self.pool.connect()

        resp = self.revoke_client(tenant="teamA")["response"]
        if resp["evict_scope"] != "pool":
            self.fail(f"expected evict_scope=pool, got {resp['evict_scope']}")
        if resp["handles_evicted"] < 1:
            self.fail(f"expected handles_evicted >= 1, got {resp['handles_evicted']}")
        self.assert_pool_handle_evicted()
        self.expect_connect_rejected(h1, "DER_BAD_CERT")
        self.expect_connect_rejected(h2, "DER_BAD_CERT")
