#!/usr/bin/env python3

import argparse
import json
import re
import pandas as pd
import github
from enum import Enum
from urllib.request import urlopen
from typing import List, Dict, Any


PARSER = argparse.ArgumentParser()
PARSER.add_argument('--prefix', type=str, default='test')
PARSER.add_argument('--gh_token', type=str, required=True)
PARSER.add_argument('--pr', type=int, required=True)
PARSER.add_argument('--jids', type=str, required=False)


JENKINS_HOME = 'https://build.hpdd.intel.com/job/daos-stack'
DAOS_REPO = 'https://github.com/daos-stack/daos.git'


def je_load(pr: int, jid=None, what=None, tree=None):
    """Fetch something from Jenkins and return as native type."""
    url = f"{JENKINS_HOME}/job/daos/job/PR-{pr}"
    if jid:
        url += f"/{jid}"
        if what:
            url += f"/{what}"
    url += "/api/json"
    if tree:
        url += f"?tree={tree}"
    with urlopen(url) as f:  # nosec
            return json.load(f)


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


def dump(data: Any, name: str) -> None:
    with open(f'{name}.json', 'w') as f:
        json.dump(data, f, indent=4)
    print(f'File has been writte: {name}.json')


def pr_get_all_jids(pr: int) -> List[int]:
    data = je_load(pr)
    return list(range(1, len(data['builds']) + 1))


PARAMS = [
    'TestTag',
    'CI_RPM_TEST_VERSION',
    'CI_medium_TEST',
    'CI_medium-md-on-ssd_TEST',
    'CI_medium-verbs-provider_TEST',
    'CI_medium-verbs-provider-md-on-ssd_TEST',
    'FUNCTIONAL_HARDWARE_MEDIUM_LABEL',
    'FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL',
]


def pr_job_parameters(actions: Dict) -> Dict:
    params = None
    for action in actions:
        if action.get('_class', '') == 'hudson.model.ParametersAction':
            params = action['parameters']
            break
    if params is None:
        print('Job parameters not found')
        return {}
    output = {}
    for param in params:
        if param['name'] in PARAMS:
            output[param['name']] = param['value']
    return output


class PARAM_TYPE(Enum):
    IGNORE = 0
    STR = 1
    BOOL = 2


PRAGMA_KEY_TO_PARAM = {
    'Cancel-prev-build':            PARAM_TYPE.IGNORE,
    'Doc-only':                     PARAM_TYPE.IGNORE,
    'Required-githooks':            PARAM_TYPE.IGNORE,
    'Skip-build-leap15-icc':        PARAM_TYPE.IGNORE,
    'Skip-build-el9-rpm':           PARAM_TYPE.IGNORE,
    'Skip-build-leap15-rpm':        PARAM_TYPE.IGNORE,
    'Skip-coverity-test':           PARAM_TYPE.IGNORE,
    'Skip-fault-injection-test':    PARAM_TYPE.IGNORE,
    'Skip-func-hw-test-large':      PARAM_TYPE.IGNORE,
    'Skip-nlt':                     PARAM_TYPE.IGNORE,
    'Skip-python-bandit':           PARAM_TYPE.IGNORE,
    'Skip-unit-test-memcheck':      PARAM_TYPE.IGNORE,
    'Skip-unit-tests':              PARAM_TYPE.IGNORE,
    'Signed-off-by':                PARAM_TYPE.IGNORE,
    'Quick-functional':             PARAM_TYPE.IGNORE,
    'Skip-build-ubuntu20-rpm':      PARAM_TYPE.IGNORE,
    'Skip-func-test-el8':           PARAM_TYPE.IGNORE,
    'Skip-el8':                     PARAM_TYPE.IGNORE,
    'Skip-func-el8':                PARAM_TYPE.IGNORE,
    'RPM-test-version':             PARAM_TYPE.STR,
    'Func-hw-test-distro':          PARAM_TYPE.STR,
    'Test-tag':                     PARAM_TYPE.STR,
    'Skip-func-hw-test-medium':                             PARAM_TYPE.BOOL,
    'Skip-func-hw-test-medium-ucx-provider':                PARAM_TYPE.BOOL,
    'Skip-func-hw-test-medium-verbs-provider':              PARAM_TYPE.BOOL,
    'Skip-func-hw-test-medium-md-on-ssd':                   PARAM_TYPE.BOOL,
    'Skip-func-hw-test-medium-verbs-provider-md-on-ssd':    PARAM_TYPE.BOOL,
}


def param_ignore(_1, _2, _3):
    pass


def param_str(k: str, v: str, params: Dict) -> None:
    params[k] = v


def param_bool(k: str, v: str, params: Dict) -> None:
    params[k] = bool(v)


PRAGMA_TO_PARAM = {
    PARAM_TYPE.IGNORE: param_ignore,
    PARAM_TYPE.STR: param_str,
    PARAM_TYPE.BOOL: param_bool,
}


def pr_job_tags(actions: Dict, repo: github.Repository, params: Dict) -> str:
    # lookup the daos-stack/daos build
    daos_build = None
    for action in actions:
        if action.get('_class', '') == 'hudson.plugins.git.util.BuildData' and \
                action['remoteUrls'][0] == DAOS_REPO:
            daos_build = action
            break
    if daos_build is None:
        print('DAOS build not found')
        return {}
    revision = daos_build['lastBuiltRevision']['SHA1']
    print(f'- https://github.com/daos-stack/daos/commit/{revision}')
    commit = repo.get_commit(revision)
    # parse the commit message
    for line in commit.commit.message.splitlines()[1:]: # skip the first line
        found = re.search('^([A-Za-z0-9-]+): (.*)$', line)
        if found:
            key = found.group(1)
            value = found.group(2)
            if key not in PRAGMA_KEY_TO_PARAM.keys():
                print(f'Unknown pragma key: {key}')
            param_type = PRAGMA_KEY_TO_PARAM[key]
            PRAGMA_TO_PARAM[param_type](key, value, params)
    return params


def pr_job_parse(pr: int, jid: int, repo: github.Repository) -> Dict:
    print(f'PR-{pr} #{jid} - parsing...')
    data = je_load(pr, jid)
    params = {}
    params = pr_job_parameters(data['actions'])
    params = pr_job_tags(data['actions'], repo, params)
    params['result'] = data['result']
    return params


def combine(jobs: Dict, fname: str) -> None:
    keys = []
    for _, job in jobs.items():
        keys = keys + list(job.keys())
    keys = list(set(keys))
    keys.sort()
    jids = list(jobs.keys())
    data = {'jid': jids}
    for key in keys:
        values = [jobs[jid].get(key, '') for jid in jids]
        data[key] = values
    df = pd.DataFrame(data=data)
    df.to_excel(f'{fname}.xlsx', index=False)
    print(f'File has been writte: {fname}.xlsx')


def get_repo(token: str) -> github.Repository:
    gh = github.Github(token)
    return gh.get_repo('daos-stack/daos')


def main():
    args = PARSER.parse_args()
    repo = get_repo(args.gh_token)
    if args.jids is None:
        jids = pr_get_all_jids(args.pr)
    else:
        jids = sequence_to_list(args.jids)
    jids_str = ','.join([str(jid) for jid in jids])
    print(f'PR-{args.pr} builds: {jids_str}')
    jobs = {jid: pr_job_parse(args.pr, jid, repo) for jid in jids}
    dump(jobs, f'{args.prefix}_jobs')
    combine(jobs, f'{args.prefix}_list')


if __name__ == "__main__":
    main()
