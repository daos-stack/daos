"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
from time import sleep

from apricot import TestWithServers
from run_utils import run_remote


class DaosServerMemUsage(TestWithServers):
    """Test memory usage after starting/stopping servers."""

    def get_memory_data(self, description):
        """Get the total, free, and available memory per server host.

        Returns:
            dict: total, free, and available memory per server host
        """
        memory_data = {}
        result = run_remote(self.log, self.server_managers[0].hosts, "free -m", stderr=True)
        if not result.passed:
            self.fail(f'Error obtaining available memory on {result.failed_hosts}')
        for data in result.output:
            hosts = str(data.hosts)
            try:
                info = re.findall('Mem:\s+(\d+)\s+\d+\s+(\d+)\s+\d+\s+\d+\s+(\d+)', data.stdout)
                memory_data[hosts] = {
                    'total': info[0], 'free': info[1], 'avail': info[2], 'description': description}
            except IndexError:
                self.log.debug('Error collecting free memory information for %s', hosts)
        return memory_data

    def test_server_memory_usage(self):
        """JIRA ID: DAOS-14203.

        Test Description:
            Start daos_server multiple times followed by the test harness cleanup and verify
            available memory is restored each time.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,daos_server
        :avocado: tags=DaosServerMemUsage,test_server_memory_usage
        """
        sequences = self.params.get('sequences', default=2)
        memory_data = []
        for index in range(sequences):
            sequence = f'{index + 1}/{sequences}'
            description = f'before starting servers {sequence}'
            self.log_step(f'Collecting memory data {description}', True)
            memory_data.append(self.get_memory_data(description))
            self.log_step(f'Starting servers {sequence}')
            self.start_servers()
            description = f'after starting servers {sequence}'
            self.log_step(f'Collecting memory data {description}', True)
            memory_data.append(self.get_memory_data(description))
            self.log_step(f'Stopping servers {sequence}')
            self.stop_servers()
            description = f'after stopping servers {sequence}'
            self.log_step(f'Collecting memory data {description}', True)
            memory_data.append(self.get_memory_data(description))
            sleep(10)
        description = 'at the end'
        self.log_step(f'Collecting memory data {description}', True)
        memory_data.append(self.get_memory_data(description))

        self.log.debug('Summery of server host memory')
        message_format = '%-27s  %-10s  %-7s  %-7s  %-7s'
        self.log.debug(message_format, 'Action', 'Host', 'Total', 'Free', 'Avail')
        self.log.debug(message_format, '-' * 27, '-' * 10, '-' * 7, '-' * 7, '-' * 7)
        for entry in memory_data:
            for index, host in enumerate(sorted(entry)):
                item = entry[host]
                self.log.debug(
                    message_format, item['description'] if index == 0 else '', host, item['total'],
                    item['free'], item['avail'])
        self.log_step('Test Passed')
