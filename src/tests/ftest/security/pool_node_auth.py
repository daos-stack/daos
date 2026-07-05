"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from node_auth_test_base import NodeAuthTestBase


class PoolNodeAuthTest(NodeAuthTestBase):
    """Tests for per-pool node certificate authentication.

    :avocado: recursive
    """

    def test_pool_cert_lifecycle(self):
        """Set-cert generate, get-cert, add-client, connect, delete-cert --all.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_pool_cert_lifecycle
        """
        self.setup_pool_with_cert_auth()

        info = self.get_dmg_command().pool_get_cert(pool=str(self.pool.uuid))
        certs = info["response"]["certificates"]
        if len(certs) != 1:
            self.fail(f"expected 1 cert after set-cert, got {len(certs)}")

        host = self.hostlist_clients[0:1]
        cert, key = self.add_client_cert(node=str(host[0]))
        self.install_node_cert(cert, key, host)
        self.expect_connect_succeeds(host)

        self.get_dmg_command().pool_delete_cert(pool=str(self.pool.uuid), delete_all=True)
        # Bundle cleared; the agent's installed cert is harmless now.
        self.remove_node_cert(host)
        self.expect_connect_succeeds(host)

    def test_pool_cert_import_mode(self):
        """Import a pre-existing CA via dmg pool set-cert --cert.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_pool_cert_import_mode
        """
        self.setup_pool_with_cert_auth()
        existing_ca = self.pool_ca_cert

        # Fresh pool, import the same CA.
        self.add_pool(connect=False)
        self.get_dmg_command().pool_set_cert(
            pool=str(self.pool.uuid), cert=existing_ca)
        info = self.get_dmg_command().pool_get_cert(pool=str(self.pool.uuid))
        if len(info["response"]["certificates"]) != 1:
            self.fail("import-mode set-cert did not install the bundle")

    def test_set_cert_requires_explicit_intent(self):
        """Re-running set-cert on a pool with CAs requires --replace or --append.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_set_cert_requires_explicit_intent
        """
        self.setup_pool_with_cert_auth()
        existing_ca = self.pool_ca_cert
        try:
            self.get_dmg_command().pool_set_cert(
                pool=str(self.pool.uuid), cert=existing_ca)
        except Exception as e:  # pylint: disable=broad-except
            if "specify --replace or --append" not in str(e):
                self.fail(f"unexpected error: {e}")
            return
        self.fail("set-cert without --replace/--append should have failed")

    def test_set_cert_replace_clears_bundle(self):
        """--replace clears the bundle so only the new CA remains.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_set_cert_replace_clears_bundle
        """
        self.setup_pool_with_cert_auth()
        original_fp = self.get_dmg_command().pool_get_cert(
            pool=str(self.pool.uuid))["response"]["certificates"][0]["fingerprint"]

        # Generate a new CA on a sibling pool, then --replace into the original.
        sibling_dir = os.path.join(self.cert_workdir, "sibling")
        os.makedirs(sibling_dir, exist_ok=True)
        original = self.pool
        self.add_pool(connect=False)
        sibling = self.pool
        sibling_uuid_lc = str(sibling.uuid).lower()
        self.get_dmg_command().pool_set_cert(
            pool=str(sibling.uuid),
            daos_ca_key=self._daos_ca_key_path(),
            output=sibling_dir)
        sibling_ca = os.path.join(sibling_dir, f"{sibling_uuid_lc}_ca.crt")

        self.pool = original
        self.get_dmg_command().pool_set_cert(
            pool=str(self.pool.uuid), cert=sibling_ca, replace=True)

        certs = self.get_dmg_command().pool_get_cert(
            pool=str(self.pool.uuid))["response"]["certificates"]
        if len(certs) != 1:
            self.fail(f"expected 1 cert after --replace, got {len(certs)}")
        if certs[0]["fingerprint"] == original_fp:
            self.fail("original CA was not replaced")

    def test_pool_cert_rejections(self):
        """Negative cases: no cert, wrong CA, CN mismatch, cross-pool.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_pool_cert_rejections
        """
        self.setup_pool_with_cert_auth()
        original_pool = self.pool
        host = self.hostlist_clients[0:1]

        # No cert installed.
        self.expect_connect_rejected(host)

        # CN=node:<wrong-host> presented from the right host.
        cert, key = self.add_client_cert(node="nonexistent-host-name")
        self.install_node_cert(cert, key, host)
        self.expect_connect_rejected(host)
        self.remove_node_cert(host)

        # Cert signed by an unrelated CA.
        other_dir = os.path.join(self.cert_workdir, "other")
        os.makedirs(other_dir, exist_ok=True)
        self.add_pool(connect=False)
        other_pool = self.pool
        other_uuid_lc = str(other_pool.uuid).lower()
        self.get_dmg_command().pool_set_cert(
            pool=str(other_pool.uuid),
            daos_ca_key=self._daos_ca_key_path(),
            output=other_dir)
        unrelated_ca_key = os.path.join(other_dir, f"{other_uuid_lc}_ca.key")
        self.get_dmg_command().pool_add_client(
            pool=str(other_pool.uuid),
            pool_ca_key=unrelated_ca_key,
            output=other_dir, node=str(host[0]))
        unrelated_cert = os.path.join(other_dir, str(host[0]),
                                      f"{other_uuid_lc}.crt")
        unrelated_key = os.path.join(other_dir, str(host[0]),
                                     f"{other_uuid_lc}.key")
        # Install the unrelated cert under the original pool's uuid path.
        self.pool = original_pool
        self.install_node_cert(unrelated_cert, unrelated_key, host)
        self.expect_connect_rejected(host)

    def test_pool_cert_multi_ca(self):
        """Bundle of 2 CAs; delete one by fingerprint; remaining still works.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,pool,pool_cert
        :avocado: tags=PoolNodeAuthTest,test_pool_cert_multi_ca
        """
        self.setup_pool_with_cert_auth()
        original_pool = self.pool
        host = self.hostlist_clients[0:1]

        # Add a second CA to the bundle. We reuse set-cert (generate mode)
        # against a second pool to get a second CA cert+key, then import
        # that CA into the first pool's bundle via dmg pool set-cert --cert.
        # In real deployments the operator's external CA would be the source.
        second_dir = os.path.join(self.cert_workdir, "second")
        os.makedirs(second_dir, exist_ok=True)
        self.add_pool(connect=False)
        second_pool = self.pool
        second_uuid_lc = str(second_pool.uuid).lower()
        self.get_dmg_command().pool_set_cert(
            pool=str(second_pool.uuid),
            daos_ca_key=self._daos_ca_key_path(),
            output=second_dir)
        second_ca_cert = os.path.join(second_dir, f"{second_uuid_lc}_ca.crt")
        second_ca_key = os.path.join(second_dir, f"{second_uuid_lc}_ca.key")

        self.pool = original_pool
        self.get_dmg_command().pool_set_cert(
            pool=str(self.pool.uuid), cert=second_ca_cert, append=True)

        info = self.get_dmg_command().pool_get_cert(pool=str(self.pool.uuid))
        certs = info["response"]["certificates"]
        if len(certs) != 2:
            self.fail(f"expected 2 certs after second set-cert, got {len(certs)}")
        first_fp = certs[0]["fingerprint"]
        second_fp = certs[1]["fingerprint"]

        # A client signed by the second CA must connect.
        self.get_dmg_command().pool_add_client(
            pool=str(self.pool.uuid),
            pool_ca_key=second_ca_key,
            output=second_dir, node=str(host[0]))
        uuid_lc = str(self.pool.uuid).lower()
        cert = os.path.join(second_dir, str(host[0]), f"{uuid_lc}.crt")
        key = os.path.join(second_dir, str(host[0]), f"{uuid_lc}.key")
        self.install_node_cert(cert, key, host)
        self.expect_connect_succeeds(host)

        # Remove the second CA. The installed client cert is signed by the
        # now-removed CA and must be rejected.
        self.get_dmg_command().pool_delete_cert(
            pool=str(self.pool.uuid), fingerprint=second_fp)
        info = self.get_dmg_command().pool_get_cert(pool=str(self.pool.uuid))
        remaining = info["response"]["certificates"]
        if len(remaining) != 1 or remaining[0]["fingerprint"] != first_fp:
            self.fail("delete-cert --fingerprint did not remove the right CA")
        self.expect_connect_rejected(host)
