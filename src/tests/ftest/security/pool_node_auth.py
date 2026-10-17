"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from exception_utils import CommandFailure
from node_auth_test_base import NodeAuthTestBase


class PoolNodeAuthTest(NodeAuthTestBase):
    """Tests for per-pool node certificate authentication.

    :avocado: recursive
    """

    def test_pool_cert_lifecycle(self):
        """node-auth enable, status, issue, connect, disable.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_pool_cert_lifecycle
        """
        self.setup_pool_with_cert_auth()

        status = self.get_dmg_command().pool_node_auth_status(pool=str(self.pool.uuid))
        if not status["response"]["enabled"]:
            self.fail("status does not report node auth enabled after enable")
        certs = status["response"]["certificates"]
        if len(certs) != 1:
            self.fail(f"expected 1 cert after enable, got {len(certs)}")

        host = self.hostlist_clients[0:1]
        cert, key = self.issue_client_cert(node=str(host[0]))
        self.install_node_cert(cert, key, host)
        # The preflight must agree with the deployment before connect.
        self.check_node_cert_on(host)
        self.expect_connect_succeeds(host)

        self.get_dmg_command().pool_node_auth_disable(pool=str(self.pool.uuid))
        status = self.get_dmg_command().pool_node_auth_status(pool=str(self.pool.uuid))
        if status["response"]["enabled"]:
            self.fail("status still reports node auth enabled after disable")
        # Server-backed preflight now reports the pool no longer requires
        # certs; the still-installed cert is harmless.
        self.check_node_cert_on(host)
        self.remove_node_cert(host)
        self.expect_connect_succeeds(host)

    def test_pool_cert_import_mode(self):
        """A pool CA and node cert minted without a system connection work.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_pool_cert_import_mode
        """
        self.pool = self.get_pool(connect=False)
        admin_ca_cert, admin_ca_key = self.generate_pool_ca("offline")
        self.get_dmg_command().pool_node_auth_enable(
            pool=str(self.pool.uuid), cert=admin_ca_cert)
        self.register_cleanup(self._disable_pool_cert_auth, pool=self.pool)
        self.pool_ca_cert, self.pool_ca_key = admin_ca_cert, admin_ca_key

        certs = self.get_dmg_command().pool_node_auth_status(
            pool=str(self.pool.uuid))["response"]["certificates"]
        expected_cn = f"DAOS Pool CA {str(self.pool.uuid).lower()}"
        if len(certs) != 1 or expected_cn not in certs[0]["subject"]:
            self.fail(f"imported CA is not the installed bundle: {certs}")

        host = self.hostlist_clients[0:1]
        cert, key = self.generate_client_cert(node=str(host[0]))
        self.install_node_cert(cert, key, host)
        self.check_node_cert_on(host)
        self.expect_connect_succeeds(host)

    def test_enable_evicts_open_handles(self):
        """enable evicts handles opened before the requirement existed.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_enable_evicts_open_handles
        """
        self.pool = self.get_pool(connect=False)
        self.pool.connect()
        resp = self.enable_pool_cert_auth()["response"]
        if resp["handles_evicted"] < 1:
            self.fail(f"expected handles_evicted >= 1, got {resp['handles_evicted']}")
        self.assert_pool_handle_evicted()
        self.expect_connect_rejected(self.hostlist_clients[0:1], "DER_NO_NODE_CERT")

    def test_pool_cert_rejections(self):
        """Negative cases: no cert, CN mismatch, CA not in the bundle, unusable file.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_pool_cert_rejections
        """
        self.setup_pool_with_cert_auth()
        host = self.hostlist_clients[0:1]

        # No cert installed: preflight flags the missing file, connect fails.
        self.check_node_cert_on(host, expect_pass=False)
        self.expect_connect_rejected(host, "DER_NO_NODE_CERT")

        # CN=node:<wrong-host> deployed on this host: the preflight flags the
        # mismatch, and the agent refuses rather than present it.
        cert, key = self.issue_client_cert(node="nonexistent-host-name")
        self.install_node_cert(cert, key, host)
        self.check_node_cert_on(host, expect_pass=False)
        self.expect_connect_rejected(host, "DER_BAD_CERT")

        # A well-formed cert from a pool CA that is not in the bundle.
        _, stale_ca_key = self.generate_pool_ca("stale")
        cert, key = self.generate_client_cert(node=str(host[0]), ca_key=stale_ca_key)
        self.install_node_cert(cert, key, host)
        self.expect_connect_rejected(host, "DER_BAD_CERT")

        # A deployed file the agent cannot use fails the connect before it
        # reaches the server, whether or not the pool requires a cert; the
        # pool itself is fine once the file is gone.
        self.install_node_cert(cert, cert, host)
        self.expect_connect_rejected(host, "DER_BAD_CERT")
        self.pool = self.get_pool(connect=False)
        self.install_node_cert(cert, cert, host)
        self.expect_connect_rejected(host, "DER_BAD_CERT")
        self.remove_node_cert(host)
        self.expect_connect_succeeds(host)

    def test_pool_cert_multi_ca(self):
        """add-ca yields a 2-CA bundle; remove-ca by fingerprint; rest works.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_pool_cert_multi_ca
        """
        self.setup_pool_with_cert_auth()
        host = self.hostlist_clients[0:1]

        second_dir = os.path.join(self.cert_workdir, "second")
        second_ca_cert, second_ca_key = self.generate_pool_ca("second")
        self.get_dmg_command().pool_node_auth_add_ca(
            pool=str(self.pool.uuid), cert=second_ca_cert)

        status = self.get_dmg_command().pool_node_auth_status(pool=str(self.pool.uuid))
        certs = status["response"]["certificates"]
        if len(certs) != 2:
            self.fail(f"expected 2 certs after add-ca, got {len(certs)}")
        first_fp = certs[0]["fingerprint"]
        second_fp = certs[1]["fingerprint"]

        # A client signed by the second CA must connect.
        issued = self.get_dmg_command().pool_node_auth_issue(
            pool=str(self.pool.uuid),
            pool_ca_key=second_ca_key,
            output=second_dir, node=str(host[0]))["response"][0]
        self.install_node_cert(issued["cert_path"], issued["key_path"], host)
        self.expect_connect_succeeds(host)

        # The installed client cert was signed by the removed CA.
        self.get_dmg_command().pool_node_auth_remove_ca(
            pool=str(self.pool.uuid), fingerprint=second_fp)
        status = self.get_dmg_command().pool_node_auth_status(pool=str(self.pool.uuid))
        remaining = status["response"]["certificates"]
        if len(remaining) != 1 or remaining[0]["fingerprint"] != first_fp:
            self.fail("remove-ca --fingerprint did not remove the right CA")
        self.expect_connect_rejected(host, "DER_BAD_CERT")

        # The last CA cannot be removed by fingerprint; that is what disable is for.
        try:
            self.get_dmg_command().pool_node_auth_remove_ca(
                pool=str(self.pool.uuid), fingerprint=first_fp)
        except CommandFailure as error:
            if "last CA" not in str(error):
                self.fail(f"unexpected error: {error}")
        else:
            self.fail("remove-ca of the last CA should have been refused")
        status = self.get_dmg_command().pool_node_auth_status(pool=str(self.pool.uuid))
        if not status["response"]["enabled"]:
            self.fail("node auth disabled by a refused remove-ca")
