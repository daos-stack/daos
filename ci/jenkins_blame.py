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
PARSER.add_argument("--fault", type=str, required=True)
PARSER.add_argument("--build", type=str, required=True)
PARSER.add_argument("--jids", type=str, required=False)


JENKINS_HOME = "https://build.hpdd.intel.com/job/daos-stack"


BLOCK_NAMES_FILTER = [
    '', # read from the parameter
    'Test Hardware'
]


def je_load(pr: str=None, jid=None, what=None, tree=None):
    """Fetch something from Jenkins and return as native type."""
    url = f"{JENKINS_HOME}/job/daos"
    if pr is not None:
        url += f'/job/{pr}'
        if jid:
            url += f"/{jid}"
            if what:
                url += f"/{what}"
    url += "/api/json"
    if tree:
        url += f"?tree={tree}"
    try:
        with urlopen(url) as f:  # nosec
                return json.load(f)
    except:
        return None


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


def get_test_results(build: str, jid: int, fault: str, report: Dict[int, str]) -> None:
    data = je_load(build, jid, what='testReport')
    if data is None:
        report[jid] = 'Not found'
        return
    for suite in data['suites']:
        if not include_block(suite['enclosingBlockNames']):
            continue
        for case in suite['cases']:
            if case['className'] == 'Hardware':
                continue
            test_class, test_tag = get_test_class_and_tag(case['name'])
            test_cfg = get_test_config(case['name'])
            test_cfg_hash = hashlib.sha1(test_cfg.encode('utf-8')).hexdigest()[:10]
            if test_tag is None:
                test_id = f'{test_class}-{test_cfg_hash}'
            else:
                test_id = f'{test_class}.{test_tag}-{test_cfg_hash}'
            if test_id != fault:
                continue
            report[jid] = case['status']
            return
    report[jid] = 'Not found'


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


def monitor_fault(build: str, jids: List[int], fault: str) -> Dict:
    builds = {}
    summary = {}
    for jid in jids:
        print(f'Parsing {build} Build #{jid}')
        get_test_results(build, jid, fault, builds)
        result = builds[jid]
        print(result)
        sum = summary.get(result, 0)
        summary[result] = sum + 1
    meta = {
        'total': len(jids),
        'block_names_filer': BLOCK_NAMES_FILTER,
        'summary': summary
    }
    output = {
        'meta': meta,
        'builds': builds
    }
    return output


def dump(data: Any, name: str) -> None:
    with open(f'{name}.json', 'w') as f:
        json.dump(data, f, indent=4)
    print(f'File has been writte: {name}.json')


def build_get_all_jids(build: int) -> List[int]:
    data = je_load(build)
    jids = [build['number'] for build in data['builds']]
    jids.sort()
    return jids


def main():
    args = PARSER.parse_args()
    BLOCK_NAMES_FILTER[0] = args.block
    data = je_load()
    jobs = []
    for job in data['jobs']:
        name = job['name']
        if name.startswith('PR-'):
            jobs.append(int(name[3:]))
    jobs.sort(reverse=True)
    dump(jobs, f'{args.prefix}_jobs')
    # for job in jobs:
    # if args.jids is None:
    #     jids = build_get_all_jids(args.build)
    #     jids_str = ','.join([str(jid) for jid in jids])
    # else:
    #     jids_str = args.jids
    #     jids = sequence_to_list(jids_str)
    # data = monitor_fault(args.build, jids, args.fault)
    # data['meta'][args.build] = jids_str
    # dump(data, f'{args.prefix}_data')


if __name__ == "__main__":
    main()
