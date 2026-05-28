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

_DAOS_MAKE_TPL = os.path.join(FTEST_DIR, "roles", "daos_dev", "templates")

# Minimal context satisfying every Jinja2 expression in daos-make.sh.j2
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


def render(**extra):
    """Render daos-make.sh.j2 with base vars plus any keyword overrides."""
    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(_DAOS_MAKE_TPL),
        undefined=jinja2.StrictUndefined,
        keep_trailing_newline=True,
    )
    ctx = {**_BASE, **extra}
    if "groups" in extra and isinstance(extra["groups"], dict):
        ctx["groups"] = {**_BASE["groups"], **extra["groups"]}
    return env.get_template("daos-make.sh.j2").render(**ctx)


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


loader = unittest.TestLoader()
suite = unittest.TestSuite()
for cls in (
    TestDaosMakeProxy,
    TestDaosMakeGoproxy,
    TestDaosMakeSconsProxyUnset,
    TestDaosMakeClientsList,
):
    suite.addTests(loader.loadTestsFromTestCase(cls))

verbosity = 2 if VERBOSE else 1
runner = unittest.TextTestRunner(verbosity=verbosity)
result = runner.run(suite)
sys.exit(0 if result.wasSuccessful() else 1)
PYEOF
