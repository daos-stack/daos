"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re

from apricot import TestWithServers

from run_utils import run_remote


class VerifyPoolSpace(TestWithServers):
    """Verify pool space with system commands.

    :avocado: recursive
    """

    def _verify_pool_space(self, description, indices):
        """."""
        pools = []
        self.log_step(' '.join(['Create', description]), True)
        for index in indices:
            namespace = os.path.join(os.sep, 'run', '_'.join(['pool', str(index)]))
            pools.append(self.get_pool(namespace=namespace))

        self.log_step(' '.join(['Write data to', description]))

        self.verify_pool_size(description, pools)

    def _query_pool_size(self, description, pools):
        """."""
        self.log_step(' '.join(['Query pool information for', description]))
        for pool in pools:
            pool.query()

    def _create_pools(self, description, indices):
        """."""
        pools = []
        self.log_step(' '.join(['Create', description]), True)
        for index in indices:
            namespace = os.path.join(os.sep, 'run', '_'.join(['pool', str(index)]))
            pools.append(self.get_pool(namespace=namespace))
        self._query_pool_size(self, description, pools)

    def _get_system_pool_size(self, description):
        """Get the pool size information from the df system command.

        Args:
            description (str): _description_

        Returns:
            dict: the df command information per server rank
        """
        system_pool_size = {}
        self.log_step(' '.join(['Collect system-level DAOS mount information for', description]))
        command = 'df -h | grep daos'
        result = run_remote(self.log, self.server_managers[0].hosts, command, stderr=True)
        if not result.passed:
            self.fail('Error collecting system level daos mount information')
        for data in result.output:
            for line in data.stdout:
                info = re.split(r'\s+', line)
                if len(info) > 5 and info[0] == 'tmpfs':
                    for rank in self.server_managers[0].get_host_ranks(data.hosts):
                        system_pool_size[rank] = {
                            'size': info[1],
                            'used': info[2],
                            'avail': info[3],
                            'use%': info[4],
                            'mount': info[5]}
        if len(system_pool_size) != len(self.server_managers[0].hosts):
            self.fail('Error obtaining system pool data for all hosts: {}'.format(system_pool_size))
        return system_pool_size

    def _compare_system_pool_size(self, description, previous_pool_size):
        """."""
        current_pool_size = self._get_system_pool_size(description)
        self.log.info('Verifying all system reported pool size is available')
        self.log.debug('  Rank  Size   Avail  Mount       Status')
        self.log.debug('  ----  -----  -----  ----------  ------')
        overall = True
        for rank in sorted(current_pool_size.keys()):
            if previous_pool_size is None:
                status = bool(current_pool_size[rank]['size'] == current_pool_size[rank]['avail'])
            else:
                status = True
            self.log.debug(
                '  %4s  %5s  %5s  %10s  %s',
                rank, current_pool_size[rank]['size'], current_pool_size[rank]['avail'],
                current_pool_size[rank]['mount'], status)
            overall &= status
        if not overall:
            self.fail('Error detected in system pools size for {}'.format(description))
        return current_pool_size

    def test_verify_pool_space(self):
        """Test ID: DAOS-3672.

        Test steps:
        1) Create a single pool on a single server and list associated storage, verify correctness
        2) Use IOR to fill containers to varying degrees of fullness, verify storage listing
        3) Create multiple pools on a single server, list associated storage, verify correctness
        4) Use IOR to fill containers to varying degrees of fullness, verify storage listing for all
           pools
        5) Create a single pool that spans many servers, list associated storage, verify correctness
        6) Use IOR to fill containers to varying degrees of fullness, verify storage listing
        7) Create multiple pools that span many servers, list associated storage, verify correctness
        8) Use IOR to fill containers to varying degrees of fullness, verify storage listing
        9) Fail one of the servers for a pool spanning many servers.  Verify the storage listing.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=VerifyPoolSpace,test_verify_pool_space
        """
        # Step #1
        # Avail == Size
        # wolf-181: tmpfs                          20G  129M   20G   1% /mnt/daos
        # wolf-183: tmpfs                          20G  129M   20G   1% /mnt/daos
        # wolf-182: tmpfs                          20G  129M   20G   1% /mnt/daos
        description = 'initial configuration w/o pools'
        previous_pool_size = self._compare_system_pool_size(description, None)
        system_initial = self._get_system_pool_size(description)
        self.log.info('Verifying all system reported pool size is available')
        self.log.debug('  Rank  Size   Avail  Mount       Status')
        self.log.debug('  ----  -----  -----  ----------  ------')
        overall = True
        for rank in sorted(system_initial.keys()):
            status = bool(system_initial[rank]['size'] == system_initial[rank]['avail'])
            self.log.debug(
                '  %4s  %5s  %5s  %10s  %s',
                rank, system_initial[rank]['size'], system_initial[rank]['avail'],
                system_initial[rank]['mount'], status)
            overall &= status
        if not overall:
            self.fail('Error detected in system pools size for {}'.format(description))

        # Step #2
        # Rank 0 has less available space
        # wolf-181: tmpfs                          20G  2.1G   18G  11% /mnt/daos
        # wolf-183: tmpfs                          20G  129M   20G   1% /mnt/daos
        # wolf-182: tmpfs                          20G  129M   20G   1% /mnt/daos
        description = 'a single pool on a single server'
        self._create_pools(description, [1])
        previous_pool_size = self._compare_system_pool_size(description, previous_pool_size)

        # daos cont create --pool=$DAOS_POOL1 --type=POSIX 
        # Repeat 3x:
        #   a.) Write data

        #   b.) No change
        #   wolf-181: tmpfs                          20G  2.1G   18G  11% /mnt/daos
        #   wolf-183: tmpfs                          20G  129M   20G   1% /mnt/daos
        #   wolf-182: tmpfs                          20G  129M   20G   1% /mnt/daos
        previous_pool_size = self._compare_system_pool_size(description, previous_pool_size)

        #   c.) dmg pool query $DAOS_POOL1 

        # Step #3

        # self._verify_pool_space('a single pool on a single server', [1])
        # self._verify_pool_space('multiple pools on a single server', [2, 3, 4])
        # self._verify_pool_space('a single pool that spans many servers', [5])
        # self._verify_pool_space('multiple pools that span many servers', [6, 7])
