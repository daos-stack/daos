"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import shutil
import subprocess
import tempfile
import time
from contextlib import ExitStack

from avocado import Test


class CartMultiproviderOneNodeTest(Test):
    """Test primary/secondary CaRT RPCs and forwarded bound bulks on one node."""

    def fabric(self, prefix, default_interface):
        """Return the configuration for one fabric."""
        path = "/run/multiprovider/"
        interface = self.params.get(f"{prefix}_interface", path, default_interface)
        return {
            "provider": self.params.get(f"{prefix}_provider", path, "ofi+tcp"),
            "interface": interface,
            "domain": self.params.get(f"{prefix}_domain", path, interface),
        }

    def wait_ready(self, path, servers):
        """Wait for rank zero to publish both attach files."""
        deadline = time.monotonic() + 90
        while not os.path.exists(path):
            if any(server.poll() is not None for server in servers):
                self.fail("A CaRT server exited before publishing attach information")
            if time.monotonic() >= deadline:
                self.fail("Timed out waiting for secondary attach information")
            time.sleep(0.2)

    @staticmethod
    def stop_servers(servers):
        """Stop only servers launched by this test."""
        for server in servers:
            if server.poll() is None:
                server.terminate()
        for server in servers:
            if server.poll() is None:
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()

    @staticmethod
    def run_client(binary, directory, fabric, env, flags):
        """Run a client with the selected fabric."""
        args = [binary, "-a", directory, "-p", fabric["provider"], "-i", fabric["interface"],
                "-d", fabric["domain"]] + flags
        subprocess.run(args, env=env, check=True, timeout=60)

    def test_multiprovider(self):
        """Check both providers against both ranks and cross-provider bulk forwarding.

        :avocado: tags=all,daily_regression,vm
        :avocado: tags=cart,rpc,one_node,multiprovider
        :avocado: tags=CartMultiproviderOneNodeTest,test_multiprovider
        """
        name = self.params.get("binary", "/run/multiprovider/", "test_multiprovider")
        binary = shutil.which(name)
        if binary is None and name == "test_multiprovider":
            binary = shutil.which(os.path.join("/usr/lib/daos/TESTING/tests", name))
        if binary is None:
            self.fail(f"Cannot find executable {name}")

        primary = self.fabric("primary", "eth0")
        secondary = self.fabric("secondary", "lo")
        env = os.environ.copy()
        env.pop("D_PORT", None)
        env.pop("CRT_SECONDARY_PROVIDER", None)
        servers = []

        with tempfile.TemporaryDirectory(prefix="cart-multiprovider-") as workdir, \
                ExitStack() as stack:
            primary_dir = os.path.join(workdir, "primary")
            secondary_dir = os.path.join(workdir, "secondary")
            os.mkdir(primary_dir)
            os.mkdir(secondary_dir)
            server_args = [
                binary, "-w", workdir, "-a", primary_dir, "-b", secondary_dir,
                "-p", f"{primary['provider']},{secondary['provider']}",
                "-i", f"{primary['interface']},{secondary['interface']}",
                "-d", f"{primary['domain']},{secondary['domain']}",
            ]
            try:
                for rank in range(2):
                    servers.append(stack.enter_context(
                        subprocess.Popen(server_args + ["-r", str(rank)], env=env)))
                ready = os.path.join(secondary_dir, "multiprovider_test.attach_info_tmp")
                self.wait_ready(ready, servers)
                self.run_client(binary, primary_dir, primary, env, [])
                self.run_client(binary, secondary_dir, secondary, env, ["-s"])
                self.run_client(binary, primary_dir, primary, env, ["-q"])
                for server in servers:
                    if server.wait(timeout=30) != 0:
                        self.fail("CaRT server exited with an error")
            finally:
                self.stop_servers(servers)
