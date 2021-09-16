#!/usr/bin/python3
'''
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

import os
import io
import yaml

# a lookup table of predefined faults
FAULTS = {
    'DAOS_CHECKSUM_UPDATE_FAIL': {
        'id': '65568',
        'probability_x': '100',
        'probability_y': '100',
        'interval': '1',
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

}


class FaultInjectionFailed(Exception):
    """Raise if FI failed."""


def write_fault_file(path, fault_list=None, on_the_fly_fault=None):
    """ Write out a fault injection config file.

        path             --where to write the file
        fault_list       --a list of strings identifying which predefined
                            faults to write out
        on_the_fly_fault --a fault dictionary that isn't predefined

        Returns the name of the file.
    """
    if fault_list is None and on_the_fly_fault is None:
        raise FaultInjectionFailed("bad parameters")

    if not os.path.exists(path):
        os.makedirs(path)
    fi_config = path + '/fi.yaml'

    with io.open(fi_config, 'w', encoding='utf8') as outfile:
        yaml.dump({'seed': '123'}, outfile, default_flow_style=False,
                  allow_unicode=True)

        fault_config = []
        if fault_list is not None:
            for fault in fault_list:
                fault_config.append(FAULTS[fault])
        if on_the_fly_fault is not None:
            fault_config.append(on_the_fly_fault)
        yaml.dump({'fault_config': fault_config}, outfile,
                  default_flow_style=False, allow_unicode=True)

    return fi_config
