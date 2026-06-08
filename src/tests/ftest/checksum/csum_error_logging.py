"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from avocado import fail_on
from daos_core_base import DaosCoreBase
from exception_utils import CommandFailure
from general_utils import get_host_data, get_journalctl_command, journalctl_time


class CsumErrorLog(DaosCoreBase):
    """
    Test Class Description: Test checksum error logging.

    :avocado: recursive
    """

    @fail_on(CommandFailure)
    def get_checksum_error_value(self, t_start, t_end):
        """Query journalctl logs and count checksum error occurrences.

        Args:
            t_start (str): The start time for the journalctl query.
            t_end (str): The end time for the journalctl query.

        Returns:
            int: the number of checksum errors found
        """
        cmd = get_journalctl_command(t_start, t_end, system=True, units="daos_server")
        results = get_host_data(self.hostlist_servers, cmd, text="journalctl",
                                error="Error gathering system log events")
        self.log.debug(results)
        str_to_match = "CSUM error"
        occurrence = 0
        for host_result in results:
            occurrence += host_result["data"].count(str_to_match)
        return occurrence

    @fail_on(CommandFailure)
    def test_csum_error_logging(self):
        """Jira ID: DAOS-3927, DAOS-18881.

        Test Description: Inject checksum errors using daos_test -z and verify that the errors are
                          logged in the system journal.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=checksum,faults,daos_test
        :avocado: tags=CsumErrorLog,test_csum_error_logging
        """
        t_start = journalctl_time()
        self.log_step('Run the test (daos_test -z)')
        self.run_subtest()
        t_end = journalctl_time()
        self.log_step('Check checksum error logs')
        checksum_errs = self.get_checksum_error_value(t_start, t_end)
        self.log.info('Checksum Errors reported:  %d', checksum_errs)
        self.assertGreater(checksum_errs, 0, 'Checksum Errors not detected')
