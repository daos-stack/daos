#!/usr/bin/env python3

import argparse
import json
import re
import hashlib
import os
import pandas as pd
import matplotlib.pyplot as plt
from urllib.request import urlopen
from urllib.error import HTTPError
from typing import List, Dict, Tuple, Any


PARSER = argparse.ArgumentParser()
PARSER.add_argument("--prefix", type=str, default='test')
PARSER.add_argument("--block", type=str, default='Functional Hardware Medium')
PARSER.add_argument("--riv", type=str, required=True)
PARSER.add_argument("--riv_jids", type=str, required=True)
PARSER.add_argument("--ref", type=str, required=True)
PARSER.add_argument("--ref_jids", type=str, required=True)


JENKINS_URL = "https://build.hpdd.intel.com/"
JENKINS_HOME = f"{JENKINS_URL}/job/daos-stack"


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


def je_load_basic(url: str, tree: str=None, api: str='/api/json', _404_allowed: bool = False) -> Any:
    url += api
    if tree:
        url += f"?tree={tree}"
    print(url)
    try:
        with urlopen(url) as f:  # nosec
                return json.load(f)
    except HTTPError as err:
        if err.getcode() == 404 and _404_allowed:
            return {}


def je_load(pr: str, jid=None, what=None, tree=None):
    """Fetch something from Jenkins and return as native type."""
    url = f"{JENKINS_HOME}/job/daos/{pr}"
    if jid:
        url += f"/{jid}"
        if what:
            url += f"/{what}"
    return je_load_basic(url)


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
    print(f'File has been written: {name}.json')


def restore(name: str) -> Any:
    with open(f'{name}.json', 'r') as f:
        return json.load(f)


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


def get_timestamp_ms(dmY: str) -> int:
    import time
    import datetime
 
    element = datetime.datetime.strptime(dmY,"%d/%m/%Y")
    tuple = element.timetuple()
    timestamp = time.mktime(tuple)
    return int(timestamp) * 1000


def isPR(name: str) -> bool:
    known_non_pr_branches = [
        '(daily-(.*)testing)',
        '(google%252F(.*))',
        '(^master$)',
        '(provider-(.*)testing(.*))',
        '(release%252F2\.[0-9])',
        '(weekly-(.*)testing)',
        '((.*)-master-(master|release-2\.[46]))',
    ]
    found = re.search('|'.join(known_non_pr_branches), name)
    return not found


def sha256(value: str) -> str:
    return hashlib.sha256(value.encode('utf-8')).hexdigest()


def main():
    # args = PARSER.parse_args()
    # BLOCK_NAMES_FILTER[0] = args.block
    # riv_jids = sequence_to_list(args.riv_jids)
    # ref_jids = sequence_to_list(args.ref_jids)
    # hashes = {}
    # riv = count_fails(args.riv, riv_jids, 'riv', hashes)
    # ref = count_fails(args.ref, ref_jids, 'ref', hashes)
    # riv['meta'][args.riv] = args.riv_jids
    # ref['meta'][args.ref] = args.ref_jids
    # dump(hashes, f'{args.prefix}_hashes')
    # dump(riv, f'{args.prefix}_riv')
    # dump(ref, f'{args.prefix}_ref')
    # combine(riv, ref, f'{args.prefix}_combined')

    # Get all jobs
    # all_jobs = je_load('')
    # dump(all_jobs, 'all_jobs')

    # Filter out just jobs used in the last quarter
    # ts = get_timestamp_ms('01/04/2024')
    # all_jobs = restore('all_jobs')
    # print(len(all_jobs['jobs']))
    # jobs_current = restore('jobs_current')
    # start = True
    # if len(jobs_current) > 0:
    #     # start_url = jobs_current[-1]
    #     start_url = 'https://build.hpdd.intel.com/job/daos-stack/job/daos/job/ci-daos-stack-pipeline-lib-PR-426-release-2.4/'
    #     start = False
    # for job in all_jobs['jobs']:
    #     url = job['url']
    #     if not start:
    #         if url == start_url:
    #             start = True
    #         print(f'{url}: skip')
    #         continue
    #     job = je_load_basic(url)
    #     if job['lastBuild'] is None:
    #         continue   
    #     build = je_load_basic(job['lastBuild']['url'])
    #     if build['timestamp'] > ts:
    #         jobs_current.append(url)
    #         dump(jobs_current, 'jobs_current')

    # jobs_current = restore('jobs_current')
    # builds = []
    # for url in jobs_current:
    #     job = je_load_basic(url)
    #     builds_entry = {
    #         'url': url,
    #         'builds': [build['number'] for build in job['builds']]
    #     }
    #     builds.append(builds_entry)
    #     dump(builds, 'builds')

    # _builds = restore('builds')
    # builds =[]
    # for build in _builds:
    #     builds.append([
    #         build['url'],
    #         len(build['builds'])
    #     ])
    # df = pd.DataFrame(data={
    #     'url': [build[0] for build in builds],
    #     'builds': [build[1] for build in builds],
    # })
    # df.to_excel(f'builds.xlsx', index=False)
    # print(f'File has been written: builds.xlsx')

    # jobs_current = restore('jobs_current')
    # jobs_current_prs = {}
    # for url in jobs_current:
    #     name = url.split('/')[-2]
    #     if not isPR(name):
    #         continue

    #     hash = sha256(url)
    #     jobs_current_prs[hash] = url
    #     # if hash in keys.keys():
    #     #     print(keys[hash])
    #     #     print(url)
    #     #     exit(1)
    #     # else:
    #     #     keys[hash] = url
    #     # job = je_load_basic(url)
    #     # dump(job, f'dumps/{hash}')
    # dump(jobs_current_prs, 'jobs_current_prs')

    # jobs_current = restore('jobs_current_prs')
    # builds = {}
    # for hash, url in jobs_current.items():
    #     job = restore(f'jobs/{hash}')
    #     builds[url] = {build['number']: sha256(build['url']) for build in job['builds']}
    # builds = dump(builds, 'builds')

    # builds = restore('builds')
    # for job_url, in_job in builds.items():
    #     for number, hash in in_job.items():
    #         if os.path.isfile(f'wfapi/{hash}.json'):
    #             print(f'{hash} - skip')
    #             continue
    #         url = f'{job_url}{number}'
    #         build = je_load_basic(url, api='/wfapi')
    #         dump(build, f'wfapi/{hash}')

    # builds = restore('builds')
    # for _, in_job in builds.items():
    #     for _, hash in in_job.items():
    #         build = restore(f'wfapi/{hash}')
    #         for stage in build['stages']:
    #             if stage['name'] in stage_names.keys():
    #                 stage_names[stage['name']] += 1
    #             else:
    #                 stage_names[stage['name']] = 1
    #         #     if stage['name'] != stage_name:
    #         #         continue
    #         #     duration.append(int(stage['durationMillis'] / 1000 / 60))
    # dump(stage_names, 'stage_names')

    # stage_names = {
    #     'Build': '^Build.*',
    #     'Fault injection testing': 'Fault injection testing.*',
    #     'Functional Hardware Large': 'Functional Hardware Large.*',
    #     'Functional Hardware Medium UCX Provider': 'Functional Hardware Medium UCX Provider',
    #     'Functional Hardware Medium': 'Functional Hardware Medium.*',
    #     'Functional Hardware Small': 'Functional Hardware Small',
    #     'Functional': 'Functional on .*',
    #     'NLT': 'NLT.*',
    #     'Unit Test': 'Unit Test.*'
    # }
    # stage_maxes = {
    #     'Build': 0,
    #     'Fault injection testing': 200,
    #     'Functional Hardware Large': 300,
    #     'Functional Hardware Medium UCX Provider': 0,
    #     'Functional Hardware Medium': 650,
    #     'Functional Hardware Small': 0,
    #     'Functional': 500,
    #     'NLT': 35,
    #     'Unit Test': 70,
    # }
    # durations = {name: [] for name, _ in stage_names.items()}
    # durations_builds = {name: [] for name, _ in stage_names.items()}
    # data = restore('stages')
    # builds_index = restore('builds')
    # ts = get_timestamp_ms('01/04/2024')
    # for job_url, builds in data.items():
    #     for build_number, stages in builds.items():
    #         build_hash = builds_index[job_url][build_number]
    #         build_data = restore(f'builds/{build_hash}')
    #         if build_data['timestamp'] < ts:
    #             continue # exclude too old
    #         for stage_name, stage in stages.items():
    #             for stage_group, pattern in stage_names.items():
    #                 found = re.search(pattern, stage_name)
    #                 if not found:
    #                     continue
    #                 stage_data = restore(f'stages/{stage[1]}')
    #                 durationMillis = 0
    #                 for node in stage_data.get('stageFlowNodes', []):
    #                     durationMillis += node['durationMillis']
    #                 duration_minutes = int(durationMillis / 1000 / 60)
    #                 if duration_minutes <= 1:
    #                     continue
    #                 if stage_maxes[stage_group] != 0 and duration_minutes > stage_maxes[stage_group]:
    #                     continue # exclude
    #                 durations[stage_group].append(duration_minutes)
    #                 durations_builds[stage_group].append(f'{job_url}{build_number}/')
    # dump(durations, 'durations')
    # dump(durations_builds, 'durations_builds')
    # for stage_group, duration in durations.items():
    #     df = pd.DataFrame(data={
    #         'duration [m]': duration,
    #         'build': durations_builds[stage_group]
    #     })
    #     df = df.sort_values(by=['duration [m]'], ascending=False)
    #     df.to_excel(f'builds_{stage_group}.xlsx', index=False)
    #     _ = df.hist(column='duration [m]', bins=10, density=True, histtype="step", cumulative=True)
    #     plt.axhline(y=0.9, color='r', linestyle='--')
    #     plt.savefig(f'hist_{stage_group}.png')


    # stage_names = {
    #     'Build': '^Build.*',
    #     # 'Fault injection testing': 'Fault injection testing.*',
    #     # 'Functional Hardware Large': 'Functional Hardware Large.*',
    #     # 'Functional Hardware Medium UCX Provider': 'Functional Hardware Medium UCX Provider',
    #     # 'Functional Hardware Medium': 'Functional Hardware Medium.*',
    #     # 'Functional Hardware Small': 'Functional Hardware Small',
    #     # 'Functional': 'Functional on .*',
    #     # 'NLT': 'NLT.*',
    #     # 'Unit Test': 'Unit Test.*'
    # }
    # stages_out = restore('stages')
    # builds = restore('builds')
    # for job_url, in_job in builds.items():
    #     build_out = {}
    #     for build_number, hash in in_job.items():
    #         build = restore(f'wfapi/{hash}')
    #         stage_out = {}
    #         for stage in build['stages']:
    #             for _, pattern in stage_names.items():
    #                 found = re.search(pattern, stage['name'])
    #                 if not found:
    #                     continue
    #                 url = stage['_links']['self']['href']
    #                 hash = sha256(url)
    #                 stage_out[stage['name']] = [url, hash]
    #         build_out[build_number] = stage_out
    #     stages_out[job_url] = build_out
    # dump(stages_out, 'stages_build')

    # stages = restore('stages')
    # for _, builds in stages.items():
    #     for _, build in builds.items():
    #         for _, stage in build.items():
    #             url = f'{JENKINS_URL}{stage[0]}'
    #             hash = stage[1]
    #             if os.path.isfile(f'stages/{hash}.json'):
    #                 print(f'{hash} - skip')
    #                 continue
    #             data = je_load_basic(url, api='', _404_allowed = True)
    #             dump(data, f'stages/{hash}')

    PARSER = argparse.ArgumentParser()
    PARSER.add_argument("--build_url", type=str, required=True)
    args = PARSER.parse_args()
    segs = args.build_url.split('/')
    build_id = segs[-2]
    job_url = '/'.join(segs[0:-2]) + '/'
    print(job_url)
    print(build_id)
    builds_index = restore('builds')
    build_record = builds_index[job_url]
    hash = build_record[build_id]
    print(hash)
    build = restore(f'builds/{hash}')
    artifacts = build['artifacts']
    for art in artifacts:
        if art['fileName'] != 'results.xml':
            continue
        
        


if __name__ == "__main__":
    main()
