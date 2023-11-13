"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re

from apricot import TestWithServers
from exception_utils import CommandFailure
from general_utils import human_to_bytes
from ior_utils import run_ior
from job_manager_utils import get_job_manager
from run_utils import run_remote


def compare_initial(rank, pool_size):
    """Determine if all of the pool size is greater than or equal to the available.

    Args:
        rank (int): server rank
        pool_size (list): list of pool_size dictionaries

    Returns:
        bool: is all of the pool size is greater than or equal to the available
    """
    current_size = human_to_bytes(pool_size[-1]['data'][rank]['size'])
    current_avail = human_to_bytes(pool_size[-1]['data'][rank]['avail'])
    return current_size >= current_avail


def compare_equal(rank, pool_size):
    """Determine if the previous value equal the current value.

    Args:
        rank (int): server rank
        pool_size (list): list of pool_size dictionaries

    Returns:
        bool: does the previous value equal the current value
    """
    previous_avail = human_to_bytes(pool_size[-2]['data'][rank]['avail'])
    current_avail = human_to_bytes(pool_size[-1]['data'][rank]['avail'])
    return previous_avail == current_avail


def compare_reduced(rank, pool_size):
    """Determine if the previous value is greater than the current value.

    Args:
        rank (int): server rank
        pool_size (list): list of pool_size dictionaries

    Returns:
        bool: does the previous value equal the current value
    """
    previous_avail = human_to_bytes(pool_size[-2]['data'][rank]['avail'])
    current_avail = human_to_bytes(pool_size[-1]['data'][rank]['avail'])
    return previous_avail > current_avail


class VerifyPoolSpace(TestWithServers):
    """Verify pool space with system commands.

    :avocado: recursive
    """

    def _query_pool_size(self, description, pools):
        """Query the pool size for the specified pools.

        Args:
            description (str): pool description
            pools (list): list of pools to query
        """
        self.log_step(f'Query pool information for {description}')
        for pool in pools:
            pool.query()

    def _create_pools(self, description, namespaces):
        """Create the specified number of pools.

        Args:
            description (str): pool description
            namespaces (list): pool namespaces

        Returns:
            list: a list of created pools
        """
        pools = []
        self.log_step(' '.join(['Create', description]), True)
        for item in namespaces:
            namespace = os.path.join(os.sep, 'run', '_'.join(['pool', 'rank', str(item)]), '*')
            pools.append(self.get_pool(namespace=namespace))
        self._query_pool_size(description, pools)
        return pools

    def _write_data(self, description, ior_kwargs, container, block_size):
        """Write data using ior to the specified pool and container.

        Args:
            description (str): pool description
            ior_kwargs (dict): arguments to use to run ior
            container (TestContainer): the container in which to write data
            block_size (str): block size to use with the ior
        """
        self.log_step(f'Writing data ({block_size} block size) to a container in {description}')
        ior_kwargs['pool'] = container.pool
        ior_kwargs['container'] = container
        ior_kwargs['ior_params']['block_size'] = block_size
        try:
            run_ior(**ior_kwargs)
        except CommandFailure as error:
            self.fail(f'IOR write to {description} failed, {error}')

    def _get_system_pool_size(self, description, scm_mounts):
        """Get the pool size information from the df system command.

        Args:
            description (str): pool description
            scm_mounts (list): mount points used by the engine ranks

        Returns:
            dict: the df command information per server rank
        """
        system_pool_size = {}
        self.log_step(f'Collect system-level DAOS mount information for {description}')
        fields = ('source', 'size', 'used', 'avail', 'pcent', 'target')
        command = f"df -BG --output={','.join(fields)} | grep -E '{'|'.join(scm_mounts)}'"
        result = run_remote(self.log, self.server_managers[0].hosts, command, stderr=True)
        if not result.passed:
            self.fail('Error collecting system level daos mount information')
        for data in result.output:
            for line in data.stdout:
                info = re.split(r'\s+', line)
                if len(info) >= len(fields):
                    for rank in self.server_managers[0].get_host_ranks(data.hosts):
                        system_pool_size[rank] = {
                            field: info[index] for index, field in enumerate(fields)}
        if len(system_pool_size) != len(self.server_managers[0].hosts):
            self.fail(f'Error obtaining system pool data for all hosts: {system_pool_size}')
        return system_pool_size

    def _compare_system_pool_size(self, pool_size, compare_methods):
        """Compare the pool size information from the system command.

        Args:
            pool_size (list): the list of pool size information
            compare_methods (list): a list of compare methods to execute per rank
        """
        self.log.info('Verifying system reported pool size for %s', pool_size[-1]['label'])
        self.log.debug(
            '  Rank  Mount       Previous (Avail/Size)  Current (Avail/Size)   Compare  Status')
        self.log.debug(
            '  ----  ----------  ---------------------  ---------------------  -------  ------')
        overall = True
        for rank in sorted(pool_size[-1]['data'].keys()):
            status = compare_methods[rank](rank, pool_size)
            current = pool_size[-1]['data'][rank]
            if len(pool_size) > 1:
                previous = pool_size[-2]['data'][rank]
            else:
                previous = {'size': 'None', 'avail': 'None'}
            if compare_methods[rank] is compare_initial:
                compare = 'cA<cS'
            elif compare_methods[rank] is compare_reduced:
                compare = 'pA>cA'
            elif compare_methods[rank] is compare_equal:
                compare = 'pA=cA'
            else:
                compare = 'None'
            self.log.debug(
                '  %4s  %-10s   %9s / %-8s   %9s / %-8s  %7s  %s',
                rank, current['target'], previous['avail'], previous['size'], current['avail'],
                current['size'], compare, status)
            overall &= status
        if not overall:
            self.fail(f"Error detected in system pools size for {pool_size[-1]['label']}")

    def _check_pool_size(self, description, pool_size, scm_mounts, compare_methods):
        """Check the system pool size information reports as expected.

        Args:
            description (str): pool description
            pool_size (list): the list of pool size information
            scm_mounts (list): mount points used by the engine ranks
            compare_methods (list): a list of compare methods to execute per rank
        """
        pool_size.append(
            {'label': description, 'data': self._get_system_pool_size(description, scm_mounts)})
        self._compare_system_pool_size(pool_size, compare_methods)

    def test_verify_pool_space(self):
        """Test ID: DAOS-3672.

        Test steps:
        1) Start servers and list associated storage, verify correctness
        2) Create a single pool on a single server and list associated storage, verify correctness
        3) Use IOR to fill containers to varying degrees of fullness, verify storage listing
        4) Create multiple pools on a single server, list associated storage, verify correctness
        5) Use IOR to fill containers to varying degrees of fullness, verify storage listing for all
           pools
        6) Create a single pool that spans many servers, list associated storage, verify correctness
        7) Use IOR to fill containers to varying degrees of fullness, verify storage listing
        8) Create multiple pools that span many servers, list associated storage, verify correctness
        9) Use IOR to fill containers to varying degrees of fullness, verify storage listing
        10) Fail one of the servers for a pool spanning many servers.  Verify the storage listing.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=VerifyPoolSpace,test_verify_pool_space
        """
        scm_mounts = set()
        for engine_params in self.server_managers[0].manager.job.yaml.engine_params:
            scm_mounts.add(engine_params.get_value('scm_mount'))
        dmg = self.get_dmg_command()
        ior_kwargs = {
            'test': self,
            'manager': get_job_manager(self, subprocess=None, timeout=120),
            'log': None,
            'hosts': self.hostlist_clients,
            'path': self.workdir,
            'slots': None,
            'group': self.server_group,
            'processes': None,
            'ppn': 8,
            'namespace': '/run/ior/*',
            'ior_params': {'block_size': None}
        }
        pools = []
        pool_size = []

        # (1) Collect initial system information
        #  - System available space should equal the free space
        description = 'initial configuration w/o pools'
        self._check_pool_size(
            description, pool_size, scm_mounts, [compare_initial, compare_initial, compare_initial])
        dmg.storage_query_usage()

        # (2) Create a single pool on a rank 0
        #  - System free space should be less on rank 0 only
        description = 'a single pool on rank 0'
        pools.extend(self._create_pools(description, [0]))
        self._check_pool_size(
            description, pool_size, scm_mounts, [compare_reduced, compare_equal, compare_equal])
        self._query_pool_size(description, pools[0:1])

        # (3) Write various amounts of data to the single pool on a single engine
        #  - System free space should not change
        container = self.get_container(pools[0])
        for block_size in ('500M', '1M', '10M', '100M'):
            self._write_data(description, ior_kwargs, container, block_size)
            data_label = f'{description} after writing data using a {block_size} block size'
            self._check_pool_size(
                data_label, pool_size, scm_mounts, [compare_equal, compare_equal, compare_equal])
            self._query_pool_size(data_label, pools[0:1])
        dmg.storage_query_usage()

        # (4) Create multiple pools on rank 1
        #  - System free space should be less on rank 1 only
        description = 'multiple pools on rank 1'
        pools.extend(self._create_pools(description, ['1_a', '1_b', '1_c']))
        self._check_pool_size(
            description, pool_size, scm_mounts, [compare_equal, compare_reduced, compare_equal])
        self._query_pool_size(description, pools[1:4])

        # (5) Write various amounts of data to the multiple pools on rank 1
        #  - System free space should not change
        for index, block_size in enumerate(('200M', '2G', '7G')):
            container = self.get_container(pools[1 + index])
            self._write_data(description, ior_kwargs, container, block_size)
            data_label = f'{description} after writing data using a {block_size} block size'
            self._check_pool_size(
                data_label, pool_size, scm_mounts, [compare_equal, compare_equal, compare_equal])
            self._query_pool_size(data_label, pools[1 + index:2 + index])
        dmg.storage_query_usage()

        # (6) Create a single pool on ranks 1 & 2
        #  - System free space should be less on rank 1 and 2
        description = 'a single pool on ranks 1 & 2'
        pools.extend(self._create_pools(description, ['1_2']))
        self._check_pool_size(
            description, pool_size, scm_mounts, [compare_equal, compare_reduced, compare_reduced])
        self._query_pool_size(description, pools[4:5])

        # (7) Write various amounts of data to the single pool on ranks 1 & 2
        #  - System free space should not change
        container = self.get_container(pools[4])
        for block_size in ('13G', '3G', '300M'):
            self._write_data(description, ior_kwargs, container, block_size)
            data_label = f'{description} after writing data using a {block_size} block size'
            self._check_pool_size(
                data_label, pool_size, scm_mounts, [compare_equal, compare_equal, compare_equal])
            self._query_pool_size(data_label, pools[4:5])
        dmg.storage_query_usage()

        # (8) Create a single pool on all ranks
        #  - System free space should be less on all ranks
        description = 'a single pool on all ranks'
        pools.extend(self._create_pools(description, ['0_1_2']))
        self._check_pool_size(
            description, pool_size, scm_mounts, [compare_reduced, compare_reduced, compare_reduced])
        self._query_pool_size(description, pools[5:6])

        # (9) Write various amounts of data to the single pool on all ranks
        #  - System free space should not change
        container = self.get_container(pools[5])
        for block_size in ('1G', '5G'):
            self._write_data(description, ior_kwargs, container, block_size)
            data_label = f'{description} after writing data using a {block_size} block size'
            self._check_pool_size(
                data_label, pool_size, scm_mounts, [compare_equal, compare_equal, compare_equal])
            self._query_pool_size(data_label, pools[5:6])
        dmg.storage_query_usage()

        # (10) Stop one of the servers for a pool spanning many servers
        description = 'all pools after stopping rank 1'
        self.log_step(f'Checking {description}', True)
        self.server_managers[0].stop_ranks([1], self.d_log)
        status = self.server_managers[0].verify_expected_states()
        if not status['expected']:
            self.fail("Rank 1 was not stopped")
        self._check_pool_size(
            description, pool_size, scm_mounts, [compare_equal, compare_equal, compare_equal])
        for index, pool in enumerate(pools):
            self.log_step(
                ' '.join(['Query pool information for', str(pool), 'after stopping rank 1']))
            with pool.no_exception():
                result = pool.query()
            if index in [0, 5]:
                # The pool query should succeed for the first pool which only targets rank 0 and the
                # last pool which targets ranks 0, 1, and 2
                if result['status'] != 0:
                    self.fail(
                        f'{pool.identifier} dmg pool query should succeed after stopping rank 1')
                self.log.debug('Pool query for %s passed as expected', pool)
            else:
                # All other pools target the stopped rank (rank 1) so the pool query should fail
                message = 'unable to find any available service ranks'
                if index == 4:
                    message = 'control.PoolQueryReq request timed out'
                if not result['error'] or message not in result['error']:
                    self.fail(
                        f'{pool.identifier} dmg pool query should fail with \'{message}\' after '
                        'stopping rank 1')
                self.log.debug('Pool query for %s failed as expected', pool)
                # Disable teardown of an inaccessible pool
                pool.skip_cleanup()
        dmg.storage_query_usage()
