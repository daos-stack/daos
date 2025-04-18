#!/usr/bin/env python3

"""Helper module to choose build/cache keys to use for GitHub actions"""

import os
import random
import string
import subprocess  # nosec
import sys
from argparse import ArgumentParser
from os.path import join

BUILD_FILES = ['site_scons/prereq_tools',
               'site_scons/components',
               'utils/build.config',
               'SConstruct',
               '.github/workflows/landing-builds.yml',
               '.dockerignore',
               'requirements.txt',
               'requirements-build.txt',
               'requirements-utest.txt',
               'ci/gha_helper.py']

COMMIT_CMD = ['git', 'rev-parse', '--short', 'HEAD']


def set_output(key, value):
    """Set a key-value pair in GitHub actions metadata"""
    env_file = os.getenv('GITHUB_OUTPUT')
    if not env_file:
        print(f'::set-output name={key}::{value}')
        return

    delim = ''.join(random.choices(string.ascii_uppercase, k=7))  # nosec
    with open(env_file, 'a') as file:
        file.write(f'{key}<<{delim}\n{value}\n{delim}\n')


def main():
    """Parse git history to load caches for GHA"""
    parser = ArgumentParser()
    parser.add_argument(
        '--base-distro',
        type=str,
        default=os.getenv('BASE_DISTRO'),
        help='name of the distro being build default=%(default)s')
    parser.add_argument(
        '--base-ref',
        type=str,
        default=os.getenv('GITHUB_BASE_REF'),
        help='base reference default=%(default)s')
    parser.add_argument(
        '--single',
        action='store_true',
        help='TODO')
    args = parser.parse_args()


    if not args.base_ref:
        # Assume this is a landing build, so just use the branch name
        target_branch = os.getenv('GITHUB_REF_NAME')
    elif args.base_ref.startswith('release/'):
        # Use either a release branch
        target_branch = args.base_ref
    else:
        # Or master
        target_branch = 'master'

    cmd = ['git', 'rev-list', '--abbrev-commit']

    if args.single:
        cmd.append('--max-count=1')
    else:
        cmd.append('--max-count=2')
    cmd.extend(['HEAD', '--'])
    cmd.extend(BUILD_FILES)

    # Check that there are no typos in the BUILD_FILES, and that it's kept up-to-date.
    for fname in BUILD_FILES:
        assert os.path.exists(fname)

    if args.base_distro:
        docker_distro = os.getenv('DOCKER_BASE', args.base_distro)

        dockerfile = f'utils/docker/Dockerfile.{docker_distro}'
        assert os.path.exists(dockerfile)
        cmd.append(dockerfile)

        install_helper = docker_distro.replace('.', '')
        install_script = join('utils', 'scripts', f'install-{install_helper}.sh')

        assert os.path.exists(install_script)
        cmd.append(dockerfile)
    else:
        cmd.append('utils/docker')
        args.base_distro = ''

    rc = subprocess.run(cmd, check=True, capture_output=True)
    commits = rc.stdout.decode('utf-8').strip()

    if args.single:
        build_hash = commits
    else:
        lines = commits.splitlines()
        if len(lines):
            build_hash = lines.pop(0)
        else:
            build_hash = 'unknown'

    if args.single:
        print('single')
        # Landings builds, embed the current commit in the hash name, load either the exact commit
        # or the most recent build with the same build scripts.
        rc = subprocess.run(COMMIT_CMD, check=True, capture_output=True)
        commit_hash = rc.stdout.decode('utf-8').strip()

        key = f'bc-{target_branch}-{args.base_distro.replace(":", "-")}-{build_hash}-{commit_hash}'
        set_output('key', key)

        restore = f'bc-{target_branch}-{args.base_distro.replace(":", "-")}-{build_hash}-{commit_hash}'
        set_output('restore', restore)

        restore_prev = f'bc-{target_branch}-{args.base_distro.replace(":", "-")}-{build_hash}'
        set_output('restore_prev', restore_prev)

    else:
        print('not single')
        # PR builds.  Do not embed the current commit in the hash name, load the most recent build
        # scripts, and fall back to the most recent version of the build script from the last week
        # or anything if that isn't found.
        key = f'bc-{target_branch}-{args.base_distro.replace(":", "-")}-{build_hash}'
        set_output('key', key)

        restore = f'bc-{target_branch}-{args.base_distro.replace(":", "-")}-{build_hash}'
        set_output('restore', restore)

        if len(lines):
            restore_prev = f'bc-{target_branch}-{args.base_distro.replace(":", "-")}-{lines[0]}'
        else:
            restore_prev = f'bc-{target_branch}-{args.base_distro.replace(":", "-")}-'
        set_output('restore_prev', restore_prev)


if __name__ == '__main__':
    main()
