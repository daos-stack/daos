"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
from time import sleep

from apricot import TestWithServers
from run_utils import run_remote


class DaosServerMemUsage(TestWithServers):
    """Test memory usage after starting/stopping servers.

    :avocado: recursive
    """

    def get_memory_data(self, description):
        """Get the total, free, and available memory per server host.

        Returns:
            dict: total, free, and available memory per server host
        """
        memory_data = {}
        result = run_remote(self.log, self.hostlist_servers, 'free -m', stderr=True)
        if not result.passed:
            self.fail(f'Error obtaining available memory on {result.failed_hosts}')
        for data in result.output:
            hosts = str(data.hosts)
            try:
                info = re.findall(
                    r'Mem:\s+(\d+)\s+\d+\s+(\d+)\s+\d+\s+\d+\s+(\d+)', '\n'.join(data.stdout))
                memory_data[hosts] = {
                    'total': info[0][0],
                    'free': info[0][1],
                    'avail': info[0][2],
                    'description': description}
            except IndexError:
                self.log.debug('Error collecting free memory information for %s: %s', hosts, info)
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
        memory_restored_min_percent = self.params.get('memory_restored_min_percent', default=95)
        wait_after_stop = self.params.get('wait_after_stop', default=10)
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
            sleep(wait_after_stop)

        # Disable attempting to stop servers in tearDown to prevent a test failure
        self.server_managers.clear()

        description = f'after waiting {wait_after_stop} seconds'
        self.log_step(f'Collecting memory data {description}', True)
        memory_data.append(self.get_memory_data(description))

        # Summarize the free -m data
        self.log_step('Summary of server host memory', True)
        message_format = '%-27s  %-10s  %-7s  %-7s  %-7s'
        self.log.debug(message_format, 'Action', 'Host', 'Total', 'Free', 'Avail')
        self.log.debug(message_format, '-' * 27, '-' * 10, '-' * 7, '-' * 7, '-' * 7)
        percent_available = {}
        for index, entry in enumerate(memory_data):
            for host in sorted(entry):
                item = entry[host]
                self.log.debug(
                    message_format, item['description'], host, item['total'], item['free'],
                    item['avail'])
                if index == 0:
                    percent_available[host] = {'initial': int(item['avail'])}
                if index == len(memory_data) - 1:
                    percent_available[host]['final'] = int(item['avail'])
                    percent_available[host]['percent'] = int(
                        (percent_available[host]['final'] / percent_available[host]['initial'])
                        * 100)

        # Verify memory is restored
        self.log_step(
            f'Percentage of available memory after starting/stopping servers {sequences} time(s)',
            True)
        message_format = '%-10s  %-7s  %-7s  %-11s'
        self.log.debug(message_format, 'Host', 'Initial', 'Final', '% Available')
        self.log.debug(message_format, '-' * 10, '-' * 7, '-' * 7, '-' * 11)
        for host in sorted(percent_available):
            self.log.debug(
                message_format, host, percent_available[host]['initial'],
                percent_available[host]['final'], percent_available[host]['percent'])
        if any(x['percent'] < memory_restored_min_percent for x in percent_available.values()):
            self.fail('Available memory not restored to at least f{memory_restored_min_percent}%')
        self.log_step('Test Passed')
