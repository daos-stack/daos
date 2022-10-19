"""
(C) Copyright 2018-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re

from ClusterShell.NodeSet import NodeSet, NodeSetParseError

from run_utils import RunException, run_local


class HostException(Exception):
    """Base exception for this module."""


def get_host_data(test, hosts_key, partition_key, reservation_key, namespace):
    """Get the host information from the test yaml file.

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


def get_hosts(log, hosts, partition, reservation):
    """Get the set of hosts defined by either a list or partition/reservation.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (object): list of host names
        partition (str): slurm partition name
        reservation (str): slurm partition reservation name

    Raises:
        HostException: if there was a problem extracting nodes from a partition

    Returns:
        NodeSet: set of nodes defined by either the list or partition/reservation

    """
    if partition is not None and hosts is None:
        # If a partition is provided instead of a set of hosts get the set of hosts from the
        # partition information
        # setattr(self, partition_key, partition)
        # setattr(self, reservation_key, reservation)
        slurm_nodes = get_partition_hosts(log, partition, reservation)
        if not slurm_nodes:
            raise HostException(
                f"No valid nodes in {partition} partition with {reservation} reservation")
        hosts = slurm_nodes

    # Convert the set of hosts from slurm or the yaml file into a NodeSet
    if isinstance(hosts, (list, tuple)):
        return NodeSet.fromlist(hosts)
    return NodeSet(hosts)


def get_partition_hosts(log, partition, reservation=None):
    """Get a list of hosts in the specified slurm partition and reservation.

    Args:
        log (logger): logger for the messages produced by this method
        partition (str): name of the partition
        reservation (str): name of reservation

    Returns:
        list: list of hosts in the specified partition

    """
    hosts = []
    if partition is not None:
        # Get the partition name information
        try:
            result = run_local(log, ["scontrol", "show", "partition", partition])
        except RunException as error:
            log.warning("Unable to obtain hosts from the %s slurm partition: %s", partition, error)
            result = None

        if result:
            # Get the list of hosts from the partition information
            output = result.stdout
            try:
                hosts = list(NodeSet(re.findall(r"\s+Nodes=(.*)", output)[0]))
            except (NodeSetParseError, IndexError):
                log.warning(
                    "Unable to obtain hosts from the %s slurm partition output: %s",
                    partition, output)
                hosts = []
            if hosts and reservation is not None:
                # Get the list of hosts from the reservation information
                try:
                    result = run_local(
                        log, ["scontrol", "show", "reservation", reservation], timeout=10)
                except RunException as error:
                    log.warning(
                        "Unable to obtain hosts from the %s slurm reservation: %s",
                        reservation, error)
                    result = None
                    hosts = []
                if result:
                    # Get the list of hosts from the reservation information
                    output = result.stdout
                    try:
                        reservation_hosts = list(NodeSet(re.findall(r"\sNodes=(\S+)", output)[0]))
                    except (NodeSetParseError, IndexError):
                        log.warning(
                            "Unable to obtain hosts from the %s slurm reservation output: %s",
                            reservation, output)
                        reservation_hosts = []
                    is_subset = set(reservation_hosts).issubset(set(hosts))
                    if reservation_hosts and is_subset:
                        hosts = reservation_hosts
                    else:
                        hosts = []
    return hosts


def get_hosts_from_yaml(test, hosts_key, partition_key, reservation_key, namespace):
    """Get a NodeSet for the hosts to use in the test.

    Args:
        test (Test): the avocado test class
        hosts_key (str): test yaml key used to obtain the set of hosts to test
        partition_key (str): test yaml key used to obtain the host partition name
        reservation_key (str): test yaml key used to obtain the host reservation name
        namespace (str): test yaml path to the keys

    Returns:
        NodeSet: the set of hosts to test obtained from the test yaml

    """
    return get_hosts(
        test.log, *get_host_data(test, hosts_key, partition_key, reservation_key, namespace))


def update_clients(log, client_partition, test_servers, test_clients):
    """Remove any servers from the set of clients if the clients were derived from a partition.

    Args:
        log (logger): logger for the messages produced by this method
        client_partition (str): slurm client partition name
        test_servers (NodeSet): set of server hosts
        test_clients (NodeSet): set of client hosts
    """
    if client_partition:
        log.debug(
            "Excluding any %s servers from the current client list: %s", test_servers, test_clients)
        test_clients.difference_update(test_servers)


def get_test_hosts(test):
    """Get the server and client hosts for this test.

    Args:
        test (Test): the avocado test class

    Returns:
        (NodeSet, NodeSet): the set of server hosts and the set of client hosts

    """
    test_servers = get_hosts_from_yaml(
        test, "test_servers", "server_partition", "server_reservation", "/run/hosts/*")

    clients, client_partition, client_reservation = get_host_data(
        test, "test_clients", "client_partition", "client_reservation", "/run/hosts/*")
    test_clients = get_hosts(test.log, clients, client_partition, client_reservation)

    # Optionally remove any servers that may have ended up in the client list.  This can occur with
    # tests using slurm partitions as the client partition is setup with all of the hosts.
    update_clients(test.log, client_partition, test_servers, test_clients)

    return test_servers, test_clients
