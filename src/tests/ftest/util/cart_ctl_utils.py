#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from command_utils_base import FormattedParameter
from command_utils_base import BasicParameter
from command_utils import ExecutableCommand

# pylint: disable=too-few-public-methods,too-many-instance-attributes
class CartCtlCommand(ExecutableCommand):
    """Defines a object representing a CartCtl command."""

    def __init__(self, namespace, command):
        """Create a CartCtl Command object."""
        super().__init__(namespace, command)

        # cmds: get_uri_cache, list_ctx, get_hostname, get_pid, set_log,
        #       set_fi_attr, add_log_msg
        #
        # set_log:
        #         Set log to mask passed via -l <mask> argument
        #
        # get_uri_cache:
        #         Print rank, tag and uri from uri cache
        #
        # list_ctx:
        #         Print # of contexts on each rank and uri for each context
        #
        # get_hostname:
        #         Print hostnames of specified ranks
        #
        # get_pid:
        #         Return pids of the specified ranks
        #
        # set_fi_attr
        #         set fault injection attributes for a fault ID. This command
        #         must be acompanied by the option
        #         --attr fault_id,max_faults,probability,err_code[,argument]
        #
        # options:
        # --group-name name
        #         specify the name of the remote group
        # --cfg_path
        #         Path to group config file
        # --rank start-end,start-end,rank,rank
        #         specify target ranks; 'all' specifies every known rank
        # -l log_mask
        #         Specify log_mask to be set remotely
        # -n
        #         don't perform 'wait for ranks' sync
        # -m 'log_message'

        # CartCtl options
        self.add_log_msg = BasicParameter("add_log_msg")
        self.sub_command_class = None
        self.group_name = FormattedParameter("--group-name {}")
        self.cfg_path = FormattedParameter("--cfg_path {}")
        self.directory = FormattedParameter("--directory {}")
        self.rank = FormattedParameter("--rank {}")
        self.l = FormattedParameter("-l {}")
        self.n = BasicParameter("-n")
        self.m = FormattedParameter("-m {}")

class CartCtl(CartCtlCommand):
    """Class defining an object of type CartCtlCommand."""

    def __init__(self):
        """Create a CartCtl object."""
        super().__init__("/run/CartCtl/*", "cart_ctl")

    def run(self):
        # pylint: disable=arguments-differ
        """Run the CartCtl command.

        Raises:
            CommandFailure: In case CartCtl run command fails

        """
        self.log.info('Starting CartCtl')

        super().run()
