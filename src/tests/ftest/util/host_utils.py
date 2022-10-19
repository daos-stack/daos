"""
(C) Copyright 2018-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re

from ClusterShell.NodeSet import NodeSet, NodeSetParseError

from run_utils import RunException, run_local, get_local_host


class HostException(Exception):
    """Base exception for this module."""


def get_host_parameters(test, hosts_key, partition_key, reservation_key, namespace):
    """Get the host parameters from the test yaml file.

    Args:
        test (Test): the avocado test class
        hosts_key (str): test yaml key used to obtain the set of hosts to test
        partition_key (str): test yaml key used to obtain the host partition name
        reservation_key (str): test yaml key used to obtain the host reservation name
        namespace (str): test yaml path to the keys

    Returns:
        (object, str, str): the hosts, partition, and reservation obtained form the test yaml

    """
    reservation_default = os.environ.get("_".join(["DAOS", reservation_key.upper()]), None)

    # Collect any host information from the test yaml
    hosts = test.params.get(hosts_key, namespace)
    partition = test.params.get(partition_key, namespace)
    reservation = test.params.get(reservation_key, namespace, reservation_default)
    if partition is not None and hosts is not None:
        test.fail(
            f"Specifying both a '{partition_key}' partition and '{hosts_key}' set of hosts is not "
            "supported!")
    return hosts, partition, reservation


def get_node_set(hosts):
    """Get a NodeSet.

    Args:
        hosts (object): hosts specified as a string, list, or tuple

    Returns:
        NodeSet: a set of hosts

    """
    if isinstance(hosts, (list, tuple)):
        return NodeSet.fromlist(hosts)
    return NodeSet(hosts)


class HostInfo():
    # pylint: disable=too-few-public-methods
    """Defines the hosts being utilized by the test."""

    def __init__(self):
        """Initialize a HostInfo object."""
        self._servers = HostRole()
        self._clients = HostRole()
        self.access_points = NodeSet()

    @property
    def all(self):
        """Get all of the server and client hosts

        Returns:
            NodeSet: all the server and client hosts

        """
        return self.servers | self.clients

    @property
    def servers(self):
        """Get the server hosts.

        Returns:
            NodeSet: the server hosts
        """
        return self._servers.hosts

    @property
    def clients(self):
        """Get the clients hosts.

        Returns:
            NodeSet: the clients hosts
        """
        return self._clients.hosts

    @property
    def server_partition(self):
        """Get the server partition information.

        Returns:
            HostPartition: the server partition
        """
        return self._servers.partition

    @property
    def client_partition(self):
        """Get the client partition information.

        Returns:
            HostPartition: the client partition
        """
        return self._clients.partition

    @property
    def clients_with_localhost(self):
        """Get the test clients including the localhost.

        Returns:
            NodeSet: test clients including the localhost

        """
        return self.clients | NodeSet(get_local_host())

    def display(self, log):
        """Display the host information.

        Args:
            log (logger): logger for the messages produced by this method
        """
        log.info("-" * 100)
        log.info("--- HOST INFORMATION ---")
        log.info("hostlist_servers:    %s", self.servers)
        log.info("hostlist_clients:    %s", self.clients)
        log.info("server_partition:    %s", self._servers.partition.name)
        log.info("client_partition:    %s", self._clients.partition.name)
        log.info("server_reservation:  %s", self._servers.partition.reservation)
        log.info("client_reservation:  %s", self._clients.partition.reservation)
        log.info("access_points:       %s", self.access_points)

    def set_hosts(self, log, server_hosts, server_partition, server_reservation, client_hosts,
                  client_partition, client_reservation):
        """Set the hosts.

        Args:
            log (logger): logger for the messages produced by this method
            server_hosts (object): hosts to define as servers
            server_partition (str): server partition name
            server_reservation (str): server reservation name
            client_hosts (object): hosts to define as clients
            client_partition (str): client partition name
            client_reservation (str): client reservation name

        """
        self._servers = HostRole(server_hosts, server_partition, server_reservation)
        self._servers.update_hosts(log)
        self._clients = HostRole(client_hosts, client_partition, client_reservation)
        self._clients.update_hosts(log)

        # Optionally remove any servers that may have ended up in the client list.  This can occur
        # with tests using slurm partitions as the client partition is setup with all of the hosts.
        self._update_clients(log)

    def set_test_hosts(self, test):
        """Get the server and client hosts for this test.

        Args:
            test (Test): the avocado test class
        """
        server_params = get_host_parameters(
            test, "test_servers", "server_partition", "server_reservation", "/run/hosts/*")
        client_params = get_host_parameters(
            test, "test_clients", "client_partition", "client_reservation", "/run/hosts/*")
        self.set_hosts(test.log, *server_params, *client_params)

    def _update_clients(self, log):
        """Remove any servers from the set of clients if the clients were derived from a partition.

        Args:
            log (logger): logger for the messages produced by this method
        """
        if self._clients.partition.hosts:
            log.debug(
                "Excluding any %s servers from the current client list: %s",
                self.servers, self.clients)
            self._clients.hosts.difference_update(self.servers)


class HostRole():
    """Defines the hosts being utilized in a specific role by the test."""

    def __init__(self, hosts=None, partition=None, reservation=None):
        """Initialize a HostRole object.

        Args:
            hosts (object, optional): hosts to define in this role. Defaults to None.
            partition (str, optional): slurm partition name. Defaults to None.
            reservation (str, optional): slurm reservation name. Defaults to None.
        """
        self._hosts = get_node_set(hosts)
        self.partition = HostPartition(partition, reservation)

    @property
    def hosts(self):
        """Get the set of hosts in the partition.

        Returns:
            NodeSet: _description_
        """
        return self._hosts

    def update_hosts(self, log):
        """Update the hosts with any hosts defined by the partition.

        Args:
            log (logger): logger for the messages produced by this method

        Raises:
            HostException: if there is a problem obtaining the hosts

        """
        self.partition.set_hosts(log)
        if self.partition.hosts:
            self._hosts = NodeSet(self.partition.hosts)


class HostPartition():
    """Defines the partition being utilized by the test."""

    def __init__(self, name=None, reservation=None):
        """Initialize a HostPartition object.

        Args:
            name (str, optional): partition name. Defaults to None.
            reservation (str, optional): reservation name. Defaults to None.
        """
        self.name = name
        self.reservation = reservation
        self._hosts = NodeSet()

    @property
    def hosts(self):
        """Get the set of hosts in the partition.

        Returns:
            NodeSet: _description_
        """
        return self._hosts

    def set_hosts(self, log):
        """Set the hosts for this partition.

        Args:
            log (logger): logger for the messages produced by this method

        Raises:
            HostException: if there is a problem obtaining the hosts

        """
        partition_hosts = self.get_partition_hosts(log)
        reservation_hosts = self.get_reservation_hosts(log)
        if reservation_hosts:
            self._hosts = partition_hosts.union(reservation_hosts)
        else:
            self._hosts = partition_hosts

    def get_partition_hosts(self, log):
        """Get the hosts defined in this partition.

        Args:
            log (logger): logger for the messages produced by this method

        Raises:
            HostException: if there is a problem obtaining the hosts from the partition

        Returns:
            NodeSet: partition hosts

        """
        if self.name is None:
            return NodeSet()

        # Get the partition name information
        try:
            result = run_local(log, ["scontrol", "show", "partition", self.name])
        except RunException as error:
            raise HostException(
                f"Unable to obtain hosts from the {self.name} slurm partition") from error

        # Get the list of hosts from the partition information
        output = result.stdout
        try:
            hosts = NodeSet(re.findall(r"\s+Nodes=(.*)", output)[0])
        except (NodeSetParseError, IndexError) as error:
            raise HostException(
                f"Unable to obtain hosts from the {self.name} slurm partition output") from error

        return hosts

    def get_reservation_hosts(self, log):
        """Get the hosts defined in this reservation.

        Args:
            log (logger): logger for the messages produced by this method

        Raises:
            HostException: if there is a problem obtaining the hosts from the reservation

        Returns:
            NodeSet: reservation hosts

        """
        if not self.reservation:
            return NodeSet()

        # Get the list of hosts from the reservation information
        try:
            result = run_local(
                log, ["scontrol", "show", "reservation", self.reservation], timeout=10)
        except RunException as error:
            raise HostException(
                f"Unable to obtain hosts from the {self.reservation} reservation") from error

        # Get the list of hosts from the reservation information
        output = result.stdout
        try:
            return NodeSet(re.findall(r"\sNodes=(\S+)", output)[0])
        except (NodeSetParseError, IndexError) as error:
            raise HostException(
                f"Unable to obtain hosts from the {self.reservation} reservation output") from error
