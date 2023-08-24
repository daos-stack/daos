"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import yaml
from general_utils import distribute_files, run_command, DaosTestError
from run_utils import get_clush_command

# a lookup table of predefined faults
#
# In addition the following fault IDs are used elsewhere
#
# 0: This is used in D_ALLOC to force memory allocation failures.
# 100: Used in dfuse to trigger an exit after initialization is complete
# 101: Used by daos_init() to disable fault id 0 for duration of daos_init
FAULTS = {
    'DAOS_CSUM_CORRUPT_DISK': {
        'id': '65574',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '5',
        'max_faults': '10'},
    'DAOS_CSUM_CORRUPT_UPDATE': {
        'id': '65568',
        'probability_x': '20',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_CSUM_CORRUPT_FETCH': {
        'id': '65569',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '5',
        'max_faults': '1'},
    'DAOS_DTX_LOST_RPC_REQUEST': {
        'id': '65587',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_DTX_LOST_RPC_REPLY': {
        'id': '65588',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_DTX_LONG_TIME_RESEND': {
        'id': '65589',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_SHARD_OBJ_UPDATE_TIMEOUT': {
        'id': '65537',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_SHARD_OBJ_FETCH_TIMEOUT': {
        'id': '65538',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_SHARD_OBJ_FAIL': {
        'id': '65539',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_OBJ_UPDATE_NOSPACE': {
        'id': '65540',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_SHARD_OBJ_RW_CRT_ERROR': {
        'id': '65541',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_OBJ_REQ_CREATE_TIMEOUT': {
        'id': '65542',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_SHARD_OBJ_UPDATE_TIMEOUT_SINGLE': {
        'id': '65543',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_OBJ_SPECIAL_SHARD': {
        'id': '65544',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_OBJ_TGT_IDX_CHANGE': {
        'id': '65545',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_POOL_CREATE_FAIL_CORPC': {
        'id': '65632',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_POOL_DESTROY_FAIL_CORPC': {
        'id': '65633',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '1'},
    'DAOS_POOL_CONNECT_FAIL_CORPC': {
        'id': '65634',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_POOL_DISCONNECT_FAIL_CORPC': {
        'id': '65635',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_POOL_QUERY_FAIL_CORPC': {
        'id': '65636',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_CONT_DESTROY_FAIL_CORPC': {
        'id': '65637',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_CONT_CLOSE_FAIL_CORPC': {
        'id': '65638',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_CONT_QUERY_FAIL_CORPC': {
        'id': '65639',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_CONT_OPEN_FAIL': {
        'id': '65640',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_DROP_SCAN': {
        'id': '65546',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_NO_HDL': {
        'id': '65547',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_DROP_OBJ': {
        'id': '65548',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_UPDATE_FAIL': {
        'id': '65549',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_STALE_POOL': {
        'id': '65550',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_TGT_IV_UPDATE_FAIL': {
        'id': '65551',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_TGT_START_FAIL': {
        'id': '65552',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_HANG': {
        'id': '65555',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_TGT_SEND_OBJS_FAIL': {
        'id': '65556',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_REBUILD_TGT_NOSPACE': {
        'id': '65559',
        'probability_x': '100',
        'probability_y': '20',
        'interval': '1',
        'max_faults': '10'},
    'DAOS_SHARD_OBJ_RW_DROP_REPLY': {
        'id': '131200',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
        'max_faults': '1'},
}


class FaultInjectionFailed(Exception):
    """Raise if FI failed."""


class FaultInjection():
    """Fault Injection

    :avocado: recursive
    """

    def __init__(self):
        super().__init__()
        self._hosts = []
        self.fault_file = None
        self._test_dir = None
        self._fault_list = []

    def write_fault_file(self, on_the_fly_fault=None):
        """Write out a fault injection config file.

        Args:
            on_the_fly_fault --a fault dictionary that isn't predefined
        """
        if self._fault_list is None and on_the_fly_fault is None:
            raise FaultInjectionFailed("bad parameters")

        fi_config = os.path.join(self._test_dir, "fi.yaml")

        with open(fi_config, 'w', encoding='utf8') as outfile:
            yaml.dump({'seed': '123'}, outfile, default_flow_style=False, allow_unicode=True)
            fault_config = []
            if self._fault_list is not None:
                for fault in self._fault_list:
                    fault_config.append(FAULTS[fault])
            if on_the_fly_fault is not None:
                fault_config.append(on_the_fly_fault)
            yaml.dump({'fault_config': fault_config}, outfile,
                      default_flow_style=False, allow_unicode=True)

        os.environ["D_FI_CONFIG"] = fi_config

        self.fault_file = fi_config

    def start(self, fault_list, test_dir):
        """Create the fault injection file to inject DAOS faults.

        Args:
            fault_list (list): List of faults to inject.
            test_dir(str) : Path to create the fault injection file.
        """
        self._fault_list = fault_list
        self._test_dir = test_dir
        if self._fault_list:
            # not using "workdir" because the huge path was messing up
            # orterun or something, could re-evaluate this later
            self.write_fault_file(None)

    def copy_fault_files(self, hosts):
        """Copy the fault injection file to all test hosts.

        Args:
            hosts (list): list of hosts to copy the fault injection file
        """
        if self._fault_list:
            self._hosts = hosts
            distribute_files(self._hosts, self.fault_file, self.fault_file)

    def stop(self):
        """Remove the fault injection file created during testing.

        Returns:
           error_list (list) : Errors during removing fault files (if any).
        """
        if not self.fault_file:
            return []

        # Remove the fault injection files on the hosts.
        error_list = []
        command = "rm -f {}".format(self.fault_file)
        if self._hosts:
            command = get_clush_command(
                self._hosts, args="-S -v", command=command, command_sudo=True)
        try:
            run_command(command, verbose=True, raise_exception=False)
        except DaosTestError as error:
            error_list.append(error)
        return error_list
