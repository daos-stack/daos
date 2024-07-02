#!/usr/bin/env python3

import argparse
import json
import re
import hashlib
import pandas as pd
from urllib.request import urlopen
from typing import List, Dict, Tuple, Any


PARSER = argparse.ArgumentParser()
PARSER.add_argument("--prefix", type=str, default='test')
PARSER.add_argument("--block", type=str, default='Functional Hardware Medium')
PARSER.add_argument("--riv", type=str, required=True)
PARSER.add_argument("--riv_jids", type=str, required=True)
PARSER.add_argument("--ref", type=str, required=True)
PARSER.add_argument("--ref_jids", type=str, required=True)


JENKINS_HOME = "https://build.hpdd.intel.com/job/daos-stack"


BLOCK_NAMES_FILTER = [
    '', # read from the parameter
    'Test Hardware'
]


BLOCK_TO_PARAM_NAME_MAP = {
    'Functional Hardware Medium': 'FUNCTIONAL_HARDWARE_MEDIUM_LABEL',
    'Functional Hardware Medium Verbs Provider': 'FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL',
    'Functional Hardware Medium UCX Provider': 'FUNCTIONAL_HARDWARE_MEDIUM_UCX_PROVIDER_LABEL',
    'Functional Hardware Medium MD on SSD': 'FUNCTIONAL_HARDWARE_MEDIUM_LABEL',
}


def je_load(pr: str, jid=None, what=None, tree=None):
    """Fetch something from Jenkins and return as native type."""
    url = f"{JENKINS_HOME}/job/daos/job/{pr}"
    if jid:
        url += f"/{jid}"
        if what:
            url += f"/{what}"
    url += "/api/json"
    if tree:
        url += f"?tree={tree}"
    with urlopen(url) as f:  # nosec
            return json.load(f)


def get_runner(pr: str, jid: int) -> str:
    param_name = BLOCK_TO_PARAM_NAME_MAP[BLOCK_NAMES_FILTER[0]]
    data = je_load(pr, jid)
    for action in data['actions']:
        # Filter out the non-parameters object
        if action.get('_class', '') != 'hudson.model.ParametersAction':
            continue
        # Lookup the runner's label
        for param in action['parameters']:
            if param['name'] == param_name:
                return param['value']
    return ''


def include_block(block_names: List[str]) -> bool:
    if len(block_names) != len(BLOCK_NAMES_FILTER):
        return False
    for name, name_filter in zip(block_names, BLOCK_NAMES_FILTER):
        if name == 'Test': # accept both 'Test' and 'Test Hardware'
            continue
        if name != name_filter:
            return False
    return True


def get_test_class_and_tag(case_name: str) -> Tuple[str,]:
    # 16-./daos_test/rebuild.py:DaosCoreTestRebuild.test_rebuild_29;run-agent_config-transport_config-daos_tests-args-daos_test-pools_created-stopped_ranks-test_name-dmg-hosts-pool-server_config-engines-0-storage-0-1-timeouts-e11d
    # 1-./daos_vol/h5_suite.py:DaosVol.test_daos_vol_mpich;run-container-daos_vol_tests-test1-hosts-job_manager-pool-server_config-engines-0-storage-0-1-8b8b
    found = re.search('^[0-9]+-[a-z0-9._\/]+:([A-Za-z]+).([a-z0-9_]+);', case_name)
    if found:
        return found.group(1), found.group(2)
    # 16-./control/dmg_telemetry_io_basic.py
    found = re.search('^[0-9]+-(.[a-z0-9_\/]+.py)', case_name)
    if found:
        return found.group(1), None
    # DAOS_PIPELINE1: Testing daos_pipeline_check
    # 41. DAOS_CSUM_REBUILD06: SV, Data bulk transfer
    # 17. DAOS_CSUM04.8: With extents in reverse order
    found = re.search('^[0-9. ]*([A-Z0-9_.]+):', case_name)
    if found:
        return found.group(1), None
    # Array 10 API: write after truncate
    # Array 11: EC Array Key Query
    found = re.search('^(Array [0-9]+)[A-Z ]*:', case_name)
    if found:
        return found.group(1), None
    # EXCEPTIONS #
    # 28. DAOS_CSUM14 - Get checksum through fetch task api
    found = re.search('^[0-9]+. ([A-Z0-9_]+) -', case_name)
    if found:
        return found.group(1), None
    # 42. Punch before insert
    found = re.search('^[0-9]+. ([A-Za-z ]+)$', case_name)
    if found:
        return found.group(1).replace(' ', '_'), None
    else:
        print(f'An unexpected case name format: {case_name}')
        exit(1)


def get_test_config(case_name: str) -> str:
    # # 16-./daos_test/rebuild.py:DaosCoreTestRebuild.test_rebuild_29;run-agent_config-transport_config-daos_tests-args-daos_test-pools_created-stopped_ranks-test_name-dmg-hosts-pool-server_config-engines-0-storage-0-1-timeouts-e11d
    found = re.search('^[0-9]+-(.*)-[0-9a-f]+', case_name)
    if found:
        # Drop the trailing hash if found
        return found.group(1)
    # 13-./aggregation/basic.py
    found = re.search('^[0-9]+-(.*)$', case_name)
    if found:
        # Drop the trailing hash if found
        return found.group(1)
    else:
        return case_name


def get_tests_failed(pr: str, jid: int, hashes: Dict) -> Tuple[List[str], Dict]:
    data = je_load(pr, jid, what='testReport')
    failed = []
    for suite in data['suites']:
        if not include_block(suite['enclosingBlockNames']):
            continue
        for case in suite['cases']:
            if case['className'] == 'Hardware':
                continue
            # Include only failed tests
            if case['status'] in ['PASSED', 'SKIPPED', 'FIXED']:
                continue
            test_class, test_tag = get_test_class_and_tag(case['name'])
            test_cfg = get_test_config(case['name'])
            test_cfg_hash = hashlib.sha1(test_cfg.encode('utf-8')).hexdigest()[:10]
            if test_tag is None:
                failed.append(f'{test_class}-{test_cfg_hash}')
            else:
                failed.append(f'{test_class}.{test_tag}-{test_cfg_hash}')
            # Detect and stop on hash conflicts
            if test_cfg_hash in hashes.keys() and hashes[test_cfg_hash] != test_cfg:
                print(f'Test config\'s hash conflict: {case["name"]} vs {hashes[test_cfg_hash]}')
                exit(1)
            hashes[test_cfg_hash] = test_cfg
    return failed, hashes


def sequence_to_list(seq: str) -> List[int]:
    output = []
    ranges = seq.split(',')
    for range_str in ranges:
        start_stop = range_str.split('-')
        if len(start_stop) == 1:
            output.append(int(start_stop[0]))
        elif len(start_stop) == 2:
            output = output + list(range(int(start_stop[0]), int(start_stop[1]) + 1))
        else:
            print(f'Format error: {seq}')
            exit(1)
    return output


def count_fails(pr: str, jids: List[int], role: str, hashes: Dict) -> Dict:
    runner = None
    fail_count = {}
    builds = {}
    for jid in jids:
        print(f'Parsing {pr} Build #{jid}')
        runner_new = get_runner(pr, jid)
        if runner is None:
            runner = runner_new
        elif runner != runner_new:
            print(f'The {role}_jids contains runs from different runners: {runner}, {runner_new}')
            exit(1)
        fails, hashes = get_tests_failed(pr, jid, hashes)
        print(f'{len(fails)} tests failed.')
        for fail in fails:
            if fail in fail_count.keys():
                fail_count[fail] += 1
            else:
                fail_count[fail] = 1
        builds[jid] = fails
    meta = {
        'runner': runner,
        'total': len(jids),
        'block_names_filer': BLOCK_NAMES_FILTER
    }
    output = {
        'meta': meta,
        'fail_count': fail_count,
        'builds': builds
    }
    return output


def dump(data: Any, name: str) -> None:
    with open(f'{name}.json', 'w') as f:
        json.dump(data, f, indent=4)
    print(f'File has been writte: {name}.json')


def combine(riv: Dict, ref: Dict, fname: str) -> None:
    cases = list(set(list(riv['fail_count'].keys()) + list(ref['fail_count'].keys())))
    riv_values = [riv['fail_count'].get(case, 0) for case in cases]
    ref_values = [ref['fail_count'].get(case, 0) for case in cases]
    df = pd.DataFrame(data={
        'cases': cases,
        'riv': riv_values,
        'ref': ref_values
    })
    df = df.sort_values(by=['riv'], ascending=False)
    df.loc[len(df)] = ['total', riv['meta']['total'], ref['meta']['total']]
    df = df.rename(columns={
        'riv': riv['meta']['runner'],
        'ref': ref['meta']['runner']
    })
    df.to_excel(f'{fname}.xlsx', index=False)
    print(f'File has been writte: {fname}.xlsx')


def main():
    args = PARSER.parse_args()
    BLOCK_NAMES_FILTER[0] = args.block
    riv_jids = sequence_to_list(args.riv_jids)
    ref_jids = sequence_to_list(args.ref_jids)
    hashes = {}
    riv = count_fails(args.riv, riv_jids, 'riv', hashes)
    ref = count_fails(args.ref, ref_jids, 'ref', hashes)
    riv['meta'][args.riv] = args.riv_jids
    ref['meta'][args.ref] = args.ref_jids
    dump(hashes, f'{args.prefix}_hashes')
    dump(riv, f'{args.prefix}_riv')
    dump(ref, f'{args.prefix}_ref')
    combine(riv, ref, f'{args.prefix}_combined')


if __name__ == "__main__":
    main()
