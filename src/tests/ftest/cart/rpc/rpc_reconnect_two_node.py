'''
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import shlex
import signal
import subprocess  # nosec
import time

from cart_utils import CartTest
from ClusterShell.NodeSet import NodeSet
from host_utils import get_local_host
from run_utils import run_remote


class CartRpcReconnectTwoNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run a multi-threaded RPC test across a server restart.

    The server, client, and test controller must share ``DAOS_TEST_SHARED_DIR``
    (or HOME when unset) for attach information, the server PID, and client phase
    markers. Use one server rank and one client process on the assigned hosts.
    The client's sender threads all share one CaRT context and remain alive
    while only the server process is replaced.

    Both server launches must use the same group name and listening port.
    Provider selection comes from YAML; the scenario is transport-independent.
    For a localhost run, override both hosts to localhost and select an available
    interface (for example lo with ofi+tcp) in a separate run configuration.

    :avocado: recursive
    """

    def setUp(self):
        """Track only processes owned by this test."""
        self.reconnect_processes = []
        self.server_pid = None
        super().setUp()

    def launch_cmd_bg(self, cmd):
        """Capture job output without leaving an undrained pipe."""
        args = shlex.split(cmd)
        if "--output-filename" in args:
            # Separate client and server incarnations even when their MPI ranks match.
            index = args.index("--output-filename") + 1
            args[index] += f".{len(self.reconnect_processes)}"
        output = os.path.join(self.outputdir, f"reconnect_job_{len(self.reconnect_processes)}")
        self.log.info("CMD: %s; output: %s", cmd, output)
        # A pipe left unread while waiting for markers can block a noisy launcher.
        with open(output, "wb") as stream:
            # pylint: disable-next=consider-using-with
            process = subprocess.Popen(args, stdout=stream, stderr=subprocess.STDOUT)
        self.reconnect_processes.append(process)
        return process

    def cleanup_processes(self):
        """Avoid the base class's system-wide launcher cleanup."""
        for process in self.reconnect_processes:
            if self.check_process(process):
                process.terminate()
                if self.wait_process(process, 10) is None:
                    process.kill()
                    process.wait()
        return []

    def wait_for_marker(self, status_file, marker, process, timeout=120):
        """Wait for a complete marker line, failing early if the client exits.

        Markers are appended rather than replaced, so polling cannot miss a phase
        that finishes between reads. RPC callbacks and transport logs are not
        used as timing assumptions.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if os.path.exists(status_file):
                with open(status_file, "r", encoding="utf-8") as status:
                    if marker in status.read().splitlines():
                        return
            if not self.check_process(process):
                self.fail(f"Client exited before reporting {marker}")
            time.sleep(0.2)
        self.fail(f"Timed out waiting for client marker {marker}")

    def wait_for_attach_file(self, attach_file, process, timeout=60):
        """Wait for the server to publish its CaRT attach information."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if os.path.exists(attach_file):
                return
            if not self.check_process(process):
                self.fail("Server exited before publishing attach information")
            time.sleep(0.2)
        self.fail("Timed out waiting for server attach information")

    def start_server(self, command, attach_file, pid_file):
        """Launch the server and wait for its attach information."""
        process = self.launch_cmd_bg(command)
        if not self.check_process(process):
            self.fail("RPC reconnect server did not launch")
        self.wait_for_attach_file(attach_file, process)
        # The server closes its PID file before publishing attach information.
        with open(pid_file, encoding="utf-8") as stream:
            self.server_pid = int(stream.read().strip())
        if self.server_pid <= 1:
            self.fail(f"Invalid server PID {self.server_pid}")
        return process

    def signal_server(self):
        """Interrupt the server itself, not unrelated jobs or the MPI launcher.

        SIGTERM deliberately bypasses CaRT shutdown; normal shutdown is reserved
        for the recovered server once the client has met its success target.
        """
        server_hosts = NodeSet(self.params.get("test_servers", "/run/hosts/*/"))
        if server_hosts in (get_local_host(), NodeSet("localhost")):
            os.kill(self.server_pid, signal.SIGTERM)
            return True
        command = f"kill -TERM {self.server_pid}"
        result = run_remote(self.log, server_hosts, command, timeout=30)
        return result.passed

    def cleanup_reconnect_test(self, client_proc, server_proc, status_file):
        """Stop any remaining test processes and remove the client status file."""
        if client_proc is not None and self.check_process(client_proc):
            client_proc.terminate()
            if self.wait_process(client_proc, 10) is None:
                client_proc.kill()
                client_proc.wait()
        if server_proc is not None and self.check_process(server_proc):
            if self.server_pid is not None and not self.signal_server():
                self.log.warning("Failed to stop the RPC reconnect server during cleanup")
            server_proc.terminate()
            if self.wait_process(server_proc, 10) is None:
                server_proc.kill()
                server_proc.wait()
        if os.path.exists(status_file):
            os.unlink(status_file)

    def test_cart_rpc_reconnect_two_node(self):
        """Verify shared-context RPCs continue after the server restarts.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,rpc,two_node,reconnect
        :avocado: tags=CartRpcReconnectTwoNodeTest,test_cart_rpc_reconnect_two_node
        """
        group_name = "rpc_reconnect_group"
        attach_file = os.path.join(self.attach_dir, group_name + ".attach_info_tmp")
        status_file = os.path.join(
            self.attach_dir, f"rpc_reconnect_{os.getpid()}.status")
        pid_file = os.path.join(self.attach_dir, f"rpc_reconnect_{os.getpid()}.pid")
        if os.path.exists(status_file):
            os.unlink(status_file)

        server_cmd = self.build_cmd(self.env, "test_servers")
        server_cmd += f" --pid_file {shlex.quote(pid_file)}"
        client_cmd = self.build_cmd(self.env, "test_clients")
        client_cmd += f" --status_file {shlex.quote(status_file)}"

        server_proc = None
        client_proc = None
        try:
            server_proc = self.start_server(server_cmd, attach_file, pid_file)

            client_proc = self.launch_cmd_bg(client_cmd)
            # Establish working traffic before inducing a disconnect.
            self.wait_for_marker(status_file, "PRE_RESTART_READY", client_proc)

            if not self.signal_server():
                self.fail("Failed to interrupt the RPC reconnect server")
            # Keep the server down until the client actually observes an RPC failure.
            self.wait_for_marker(status_file, "SERVER_OUTAGE_OBSERVED", client_proc)

            server_rc = self.wait_process(server_proc, 30)
            if server_rc is None:
                self.fail("Interrupted server job did not exit")
            server_proc = None

            # SIGTERM leaves stale attach info; require fresh publication on restart.
            # The live client stays attached and does not reload this file.
            os.unlink(attach_file)
            server_proc = self.start_server(server_cmd, attach_file, pid_file)

            self.wait_for_marker(status_file, "POST_RESTART_COMPLETE", client_proc)
            # A marker is not sufficient: final RPC shutdown and cleanup must succeed.
            client_rc = self.wait_process(client_proc, 30)
            if client_rc != 0:
                self.fail(f"RPC reconnect client failed with return code {client_rc}")
            client_proc = None

            server_rc = self.wait_process(server_proc, 30)
            if server_rc != 0:
                self.fail(f"RPC reconnect server failed with return code {server_rc}")
            server_proc = None
        finally:
            self.cleanup_reconnect_test(client_proc, server_proc, status_file)
            if os.path.exists(pid_file):
                os.unlink(pid_file)

        self.log_check()
