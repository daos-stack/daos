#!/usr/bin/env bash
# Template unit tests for Jinja2 templates in the DAOS ftest Ansible playbook.
#
# Tests rendered shell-script content directly with Jinja2 — no Docker, no
# Molecule, no network access required.  Completes in under a second.
#
# Usage:
#   scripts/test-templates.sh [-v]
#
# Requirements:
#   pip install jinja2   (already in requirements.txt)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FTEST_DIR="$(dirname "${SCRIPT_DIR}")"

VERBOSE=false
[[ "${1:-}" == "-v" ]] && VERBOSE=true

python3 - "${FTEST_DIR}" "${VERBOSE}" << 'PYEOF'
"""
Template unit tests for Jinja2 templates in the DAOS ftest Ansible playbook.
"""

import os
import sys
import unittest

try:
    import jinja2
except ImportError:
    sys.exit("ERROR: jinja2 is required.  Install it with: pip install jinja2")

FTEST_DIR = sys.argv[1]
VERBOSE = sys.argv[2].lower() == "true"

_TPL_DIR = os.path.join(FTEST_DIR, "roles", "daos_dev", "templates")

# Minimal context satisfying every Jinja2 expression in the templates
_BASE = dict(
    daos_runtime_dir="/opt/daos-install",
    daos_source_dir="/home/user/daos",
    daos_build_dir="/scratch/daos-build",
    groups={
        "daos_dev": ["dev-node"],
        "daos_servers": ["srv-node"],
        "daos_clients": [],
    },
)

# Extra context required by daos-deploy-info.sh.j2 (values that Ansible
# computes from lookups/facts before passing to the template task)
_DEPLOY_INFO_VARS = dict(
    _deploy_timestamp="2024-01-15T10:30:00Z",
    _deploy_controller_host="ctrl.example.com",
    _deploy_controller_user="testuser",
    _deploy_ansible_version="2.14.3",
    _deploy_inventory_path="/home/testuser/my-inventory.yml",
)


def _env():
    """Return a Jinja2 Environment backed by the daos_dev templates directory."""
    return jinja2.Environment(
        loader=jinja2.FileSystemLoader(_TPL_DIR),
        undefined=jinja2.StrictUndefined,
        keep_trailing_newline=True,
    )


def render(**extra):
    """Render daos-make.sh.j2 with base vars plus any keyword overrides."""
    ctx = {**_BASE, **extra}
    if "groups" in extra and isinstance(extra["groups"], dict):
        ctx["groups"] = {**_BASE["groups"], **extra["groups"]}
    return _env().get_template("daos-make.sh.j2").render(**ctx)


def render_deploy_info(**extra):
    """Render daos-deploy-info.sh.j2 with base + deploy-info vars + overrides."""
    ctx = {**_BASE, **_DEPLOY_INFO_VARS, **extra}
    if "groups" in extra and isinstance(extra["groups"], dict):
        ctx["groups"] = {**_BASE["groups"], **extra["groups"]}
    return _env().get_template("daos-deploy-info.sh.j2").render(**ctx)


# ── daos-make.sh.j2 tests ─────────────────────────────────────────────────────

class TestDaosMakeProxy(unittest.TestCase):
    """Verify proxy variable rendering in daos-make.sh.j2."""

    def setUp(self):
        """Render four variants: full proxy, no bypass, empty proxy, absent proxy."""
        self.script_proxy = render(
            daos_http_proxy="http://proxy.example.com:3128",
            daos_proxy_bypass="localhost,127.0.0.1,.corp.example.com",
            daos_goproxy="http://goproxy.corp.example.com",
        )
        self.script_no_bypass = render(
            daos_http_proxy="http://proxy.example.com:3128",
        )
        self.script_empty = render(daos_http_proxy="")
        self.script_absent = render()

    def test_proxy_exports_when_http_proxy_set(self):
        """All six proxy env-vars are exported when daos_http_proxy is non-empty."""
        url = "http://proxy.example.com:3128"
        for var in ("http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY"):
            self.assertIn(f'export {var}="{url}"', self.script_proxy)
        for var in ("no_proxy", "NO_PROXY"):
            self.assertIn(
                f'export {var}="localhost,127.0.0.1,.corp.example.com"',
                self.script_proxy,
            )

    def test_no_proxy_fallback_when_bypass_not_set(self):
        """no_proxy / NO_PROXY fall back to localhost,127.0.0.1 when bypass absent."""
        self.assertIn('export no_proxy="localhost,127.0.0.1"', self.script_no_bypass)
        self.assertIn('export NO_PROXY="localhost,127.0.0.1"', self.script_no_bypass)

    def test_proxy_absent_when_empty_string(self):
        """Proxy exports must be absent when daos_http_proxy is an empty string."""
        for var in ("http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY",
                    "no_proxy", "NO_PROXY"):
            self.assertNotIn(f"export {var}=", self.script_empty)

    def test_proxy_absent_when_not_defined(self):
        """Proxy exports must be absent when daos_http_proxy is not in the context."""
        for var in ("http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY",
                    "no_proxy", "NO_PROXY"):
            self.assertNotIn(f"export {var}=", self.script_absent)


class TestDaosMakeGoproxy(unittest.TestCase):
    """GOPROXY is always rendered in daos-make.sh.j2 regardless of HTTP proxy."""

    def test_goproxy_default_when_vars_absent(self):
        """GOPROXY defaults to direct when daos_goproxy is not defined."""
        self.assertIn('export GOPROXY="direct"', render())

    def test_goproxy_default_when_proxy_empty(self):
        """GOPROXY defaults to direct when daos_http_proxy is empty."""
        self.assertIn('export GOPROXY="direct"', render(daos_http_proxy=""))

    def test_goproxy_default_when_http_proxy_set_but_goproxy_absent(self):
        """GOPROXY defaults to direct when only daos_http_proxy is provided."""
        script = render(daos_http_proxy="http://proxy.example.com:3128")
        self.assertIn('export GOPROXY="direct"', script)

    def test_goproxy_custom_value(self):
        """Custom daos_goproxy value is rendered verbatim into the script."""
        script = render(
            daos_http_proxy="http://proxy.example.com:3128",
            daos_goproxy="https://goproxy.corp.example.com,direct",
        )
        self.assertIn(
            'export GOPROXY="https://goproxy.corp.example.com,direct"', script
        )


class TestDaosMakeSconsProxyUnset(unittest.TestCase):
    """The scons compile step clears proxy so the compiler cannot use the network."""

    def test_scons_unsets_all_proxy_vars(self):
        """env --unset=X is present for all four proxy variable names."""
        script = render(daos_http_proxy="http://proxy.example.com:3128")
        for var in ("http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY"):
            self.assertIn(f"--unset={var}", script)


class TestDaosMakeClientsList(unittest.TestCase):
    """CLIENTS_LIST is conditionally rendered based on the daos_clients group."""

    def test_clients_list_absent_when_group_is_empty(self):
        """CLIENTS_LIST must not appear when daos_clients group has no hosts."""
        self.assertNotIn("CLIENTS_LIST=", render())

    def test_clients_list_present_when_group_is_non_empty(self):
        """CLIENTS_LIST is rendered as a comma-separated list of client hostnames."""
        script = render(groups={"daos_clients": ["cli1", "cli2"]})
        self.assertIn('CLIENTS_LIST="cli1,cli2"', script)


# ── daos-deploy-info.sh.j2 tests ──────────────────────────────────────────────

class TestDeployInfo(unittest.TestCase):
    """Verify provenance metadata rendering in daos-deploy-info.sh.j2."""

    def setUp(self):
        """Render with and without a clients group."""
        self.script = render_deploy_info()
        self.script_with_clients = render_deploy_info(
            groups={"daos_clients": ["cli1", "cli2"]}
        )

    def test_timestamp_is_rendered(self):
        """Generated timestamp is baked into the script."""
        self.assertIn('DAOS_GENERATED="2024-01-15T10:30:00Z"', self.script)

    def test_controller_host_is_rendered(self):
        """Controller hostname is baked into the script."""
        self.assertIn('DAOS_CONTROLLER_HOST="ctrl.example.com"', self.script)

    def test_controller_user_is_rendered(self):
        """Controller username is baked into the script."""
        self.assertIn('DAOS_CONTROLLER_USER="testuser"', self.script)

    def test_ansible_version_is_rendered(self):
        """Ansible version is baked into the script."""
        self.assertIn('DAOS_ANSIBLE_VERSION="2.14.3"', self.script)

    def test_inventory_path_is_rendered(self):
        """Inventory path is baked into the script."""
        self.assertIn(
            'DAOS_INVENTORY="/home/testuser/my-inventory.yml"', self.script
        )

    def test_inventory_copy_path_uses_runtime_dir(self):
        """Inventory copy path points inside daos_runtime_dir."""
        self.assertIn(
            'DAOS_INVENTORY_COPY="/opt/daos-install/inventory.yml"', self.script
        )

    def test_dev_nodes_rendered(self):
        """DAOS_DEV_NODES contains the dev node list."""
        self.assertIn('DAOS_DEV_NODES="dev-node"', self.script)

    def test_server_nodes_rendered(self):
        """DAOS_SERVER_NODES contains the server node list."""
        self.assertIn('DAOS_SERVER_NODES="srv-node"', self.script)

    def test_client_nodes_empty_when_no_clients(self):
        """DAOS_CLIENT_NODES is empty string when daos_clients group is empty."""
        self.assertIn('DAOS_CLIENT_NODES=""', self.script)

    def test_client_nodes_rendered_when_present(self):
        """DAOS_CLIENT_NODES is comma-joined when clients are present."""
        self.assertIn('DAOS_CLIENT_NODES="cli1,cli2"', self.script_with_clients)

    def test_sourced_guard_is_present(self):
        """Script has BASH_SOURCE guard so sourcing it causes no output."""
        self.assertIn('if [[ "${BASH_SOURCE[0]}" == "${0}" ]]', self.script)

    def test_inventory_copy_cat_is_present(self):
        """Script prints inventory content when run directly."""
        self.assertIn("cat", self.script)
        self.assertIn("DAOS_INVENTORY_COPY", self.script)


loader = unittest.TestLoader()
suite = unittest.TestSuite()
for cls in (
    TestDaosMakeProxy,
    TestDaosMakeGoproxy,
    TestDaosMakeSconsProxyUnset,
    TestDaosMakeClientsList,
    TestDeployInfo,
):
    suite.addTests(loader.loadTestsFromTestCase(cls))

verbosity = 2 if VERBOSE else 1
runner = unittest.TextTestRunner(verbosity=verbosity)
result = runner.run(suite)
sys.exit(0 if result.wasSuccessful() else 1)
PYEOF
