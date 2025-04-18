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
    args = parser.parse_args()

    cmd = ['git', 'rev-list', '--abbrev-commit', '--max-count=1']

    if args.base_ref:
        # Assume this is a PR if we have a base reference.
        # For non-landing builds, filter out anything older than 1 hour
        # to reduce the chance of a cache miss if the latest is not pushed yet
        cmd.append('--until="$(date --date "- 1 hour" +"%D %T")"')

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

    # Imbed the SHA of the latest build changes in the image name.
    rc = subprocess.run(cmd, check=True, capture_output=True)
    build_sha = rc.stdout.decode('utf-8').strip() or 'unknown'
    image_name = f'bc-{args.base_distro}-{build_sha}'
    image_name = image_name.replace(":", "-").replace("/", "-")
    set_output('image_name', image_name)

    if args.base_ref:
        # Assume this is a PR if we have a base reference.
        # Use the latest tag for PRs
        set_output('image_tag', 'latest')
    else:
        # Using the current SHA as the tag for landing runs
        rc = subprocess.run(COMMIT_CMD, check=True, capture_output=True)
        commit_hash = rc.stdout.decode('utf-8').strip()
        set_output('image_tag', commit_hash)


if __name__ == '__main__':
    main()
