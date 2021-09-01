#!/usr/bin/python
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
from ClusterShell.NodeSet import NodeSet


class ScrubberUtils():
    def __init__(self, dmg, servers):
        """Create a ScrubberUtils object.

        Args:
            dmg (DmgCommand): the DmgCommand object configured to communicate
                with the servers
            servers (list): a list of server host names
        """
        self.log = getLogger(__name__)
        self.dmg = dmg
        self.hosts = NodeSet.fromlist(servers)

    def is_scrubber_started(self, host=None):
        TEST_METRICS = ["pool_target_scrubber_scrubber_started",
                        "pool_target_scrubber_csums_total"]
        if host is None:
            # Use the first host
            host = self.hosts[0]
        else:
            host = host
            info = self.get_metrics(",".join(TEST_METRICS))
            self.log.info(info)

    def get_scrub_corrupt_metrics(self, host=None):
        CORRUPT_METRICS = "pool_target_scrubber_corruption_current"
        if host is None:
            # Use the first host
            host = self.hosts[0]
        else:
            host = host
            info = self.get_metrics(",".join(CORRUPT_METRICS))
            self.log.info(info)

    def get_csum_total_metrics(self, host=None):
        CSUM_METRICS = "pool_target_scrubber_csums_total"
        if host is None:
            # Use the first host
            host = self.hosts[0]
        else:
            host = host
            info = self.get_metrics(",".join(CSUM_METRICS))
            self.log.info(info)

