"""
(C) Copyright 2018-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from socket import gethostname

from ClusterShell.NodeSet import NodeSet
# pylint: disable=import-error,no-name-in-module
from util.slurm_utils import SlurmFailed, get_partition_hosts, get_reservation_hosts


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


def get_local_host():
    """Get the local host name.

    Returns:
        NodeSet: a NodeSet including the local host

    """
    return get_node_set(gethostname().split(".")[0])


class HostInfo():
    # pylint: disable=too-few-public-methods
    """Defines the hosts being utilized by the test."""

    def __init__(self):
        """Initialize a HostInfo object."""
        self._servers = HostRole()
        self._clients = HostRole()
        self.access_points = NodeSet()

    @property
    def all_hosts(self):
        """Get all of the server and client hosts.

        Returns:
            NodeSet: all the server and client hosts

        """
        return self.servers.hosts | self.clients.hosts

    @property
    def servers(self):
        """Get the server role.

        Returns:
            HostRole: the server role
        """
        return self._servers

    @property
    def clients(self):
        """Get the client role.

        Returns:
            HostRole: the client role
        """
        return self._clients

    def display(self, log):
        """Display the host information.

        Args:
            log (logger): logger for the messages produced by this method
        """
        log.info("-" * 100)
        log.info("--- HOST INFORMATION ---")
        log.info("servers:             %s", self.servers.hosts)
        log.info("clients:             %s", self.clients.hosts)
        log.info("server_partition:    %s", self.servers.partition.name)
        log.info("client_partition:    %s", self.clients.partition.name)
        log.info("server_reservation:  %s", self.servers.partition.reservation)
        log.info("client_reservation:  %s", self.clients.partition.reservation)
        log.info("access_points:       %s", self.access_points)

    def set_hosts(self, log, control_host, server_hosts, server_partition, server_reservation,
                  client_hosts, client_partition, client_reservation, include_local_host=False):
        """Set the host information.

        Args:
            log (logger): logger for the messages produced by this method
            control_host (object): slurm control host
            server_hosts (object): hosts to define as servers
            server_partition (str): server partition name
            server_reservation (str): server reservation name
            client_hosts (object): hosts to define as clients
            client_partition (str): client partition name
            client_reservation (str): client reservation name
            include_local_host (bool, optional): option to include the local host as a client.
                Defaults to False.

        Raises:
            HostException: if there is a problem obtaining the hosts

        """
        try:
            self._servers = HostRole(
                server_hosts, server_partition, server_reservation,
                get_partition_hosts(log, control_host, server_partition),
                get_reservation_hosts(log, control_host, server_reservation))
        except SlurmFailed as error:
            raise HostException("Error defining the server hosts") from error

        try:
            self._clients = HostRole(
                client_hosts, client_partition, client_reservation,
                get_partition_hosts(log, control_host, client_partition),
                get_reservation_hosts(log, control_host, client_reservation),
                include_local_host)
        except SlurmFailed as error:
            raise HostException("Error defining the server hosts") from error

        # Optionally remove any servers that may have ended up in the client list.  This can occur
        # with tests using slurm partitions as the client partition is setup with all of the hosts.
        if self._clients.partition.hosts:
            log.debug(
                "Excluding any %s servers from the current client list: %s",
                self.servers.hosts, self.clients.hosts)
            self._clients.hosts.difference_update(self.servers.hosts)


class HostRole():
    """Defines the hosts being utilized in a specific role by the test."""

    def __init__(self, hosts=None, partition=None, reservation=None, partition_hosts=None,
                 reservation_hosts=None, include_local_host=False):
        """Initialize a HostRole object.

        Args:
            hosts (object, optional): hosts to define in this role. Defaults to None.
            partition (str, optional): slurm partition name. Defaults to None.
            reservation (str, optional): slurm reservation name. Defaults to None.
            partition_hosts (NodeSet, optional): hosts defined in the reservation. Defaults to None.
            reservation_hosts (NodeSet, optional): hosts defined in the reservation. Defaults to
                None.
            include_local_host (bool): option to include the local host. Defaults to False.
        """
        self._partition = HostPartition(partition, partition_hosts, reservation, reservation_hosts)
        if self.partition.hosts:
            self._hosts = get_node_set(self.partition.hosts)
        else:
            self._hosts = get_node_set(hosts)
        if include_local_host:
            self._hosts.add(get_local_host())

    @property
    def hosts(self):
        """Get the set of hosts in this role.

        Returns:
            NodeSet: set of hosts in this role
        """
        return self._hosts

    @property
    def partition(self):
        """Get the partition information for this role.

        Returns:
            NodeSet: partition information for this role
        """
        return self._partition


class HostPartition():
    # pylint: disable=too-few-public-methods
    """Defines the slurm partition being utilized by the test."""

    def __init__(self, name, hosts, reservation=None, reservation_hosts=None):
        """Initialize a HostPartition object.

        Args:
            name (str): slurm partition name.
            hosts (NodeSet): hosts defined in the slurm partition.
            reservation (str, optional): slurm partition reservation name. Defaults to None.
            reservation_hosts (NodeSet, optional): hosts defined in the slurm partition reservation.
                Defaults to None.
        """
        self.name = name
        self._hosts = get_node_set(hosts)
        self.reservation = reservation
        self.reservation_hosts = get_node_set(reservation_hosts)

    @property
    def hosts(self):
        """Get the set of hosts in the slurm partition.

        Returns:
            NodeSet: hosts in the slurm partition
        """
        if self.reservation and self.reservation_hosts:
            return self._hosts.union(self.reservation_hosts)
        return self._hosts
