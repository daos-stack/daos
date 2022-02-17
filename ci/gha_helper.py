#!/usr/bin/python3

"""Helper module to choose build/cache keys to use for github actions"""

import os
import sys
from os.path import join
import subprocess #nosec

BUILD_FILES = ['site_scons',
               'utils/build.config',
               'SConstruct',
               '.github/workflows/landing-builds.yml',
               '.dockerignore',
               'ci/gha_helper.py']

COMMIT_CMD = ['git', 'rev-parse', '--short', 'HEAD']

def set_output(key, value):
    """ Set a key-value pair in github actions metadata"""

    print('::set-output name={}::{}'.format(key, value))

def main():
    """Parse git histrory to load caches for GHA"""

    # Try and use the right hash key.  For chained PRs on release branches this won't be correct
    # however most of the time it should be right, and the build should still work on cache miss
    # although it will take longer.
    base_ref = os.getenv('GITHUB_BASE_REF', None)
    if base_ref:
        # If it's a PR check if it's against a release, if so use it else use master.
        if base_ref.startswith('release/'):
            target_branch = base_ref
        else:
            target_branch = 'master'
    else:
        # If this isn't a PR then it must be a landing build, so simply use the branch name.
        target_branch = os.getenv('GITHUB_REF_NAME')

    single = '--single' in sys.argv

    # The base distro, this is the name of the distro being built, and therefore the name of the
    # hash key, but it may not be the name of the dockerfile.
    base_distro = os.getenv('BASE_DISTRO', None)

    cmd = ['git', 'rev-list', '--abbrev-commit']

    if single:
        cmd.append('--max-count=1')
    else:
        cmd.append('--max-count=2')
    cmd.extend(['HEAD', '--'])
    cmd.extend(BUILD_FILES)

    # Check that there are no typos in the BUILD_FILES, and that it's kept up-to-date.
    for fname in BUILD_FILES:
        assert os.path.exists(fname)

    if base_distro:
        docker_distro = os.getenv('DOCKER_BASE', base_distro)

        dockerfile = 'utils/docker/Dockerfile.{}'.format(docker_distro)
        assert os.path.exists(dockerfile)
        cmd.append(dockerfile)

        install_helper = docker_distro.replace('.', '')
        if install_helper == 'ubuntu2004':
            install_helper = install_helper[:-2]
        install_script = join('utils', 'scripts', 'install-{}.sh'.format(install_helper))

        assert os.path.exists(install_script)
        cmd.append(dockerfile)
    else:
        cmd.append('utils/docker')
        base_distro = ''

    rc = subprocess.run(cmd, check=True, capture_output=True)
    commits = rc.stdout.decode('utf-8').strip()

    if single:
        build_hash = commits
    else:
        lines = commits.splitlines()
        if len(lines):
            build_hash = lines.pop(0)
        else:
            build_hash = 'unknown'

    if single:
        # Landings builds, embed the current commit in the hash name, load either the exact commit
        # or the most recent build with the same build scripts.
        rc = subprocess.run(COMMIT_CMD, check=True, capture_output=True)
        commit_hash = rc.stdout.decode('utf-8').strip()

        key = 'bc-{}-{}-{}-{}-{}'.format(target_branch,
                                         base_distro, build_hash, commit_hash, '{hash}')
        set_output('key', key)

        restore = 'bc-{}-{}-{}-{}'.format(target_branch, base_distro, build_hash, commit_hash)
        set_output('restore', restore)

        restore_prev = 'bc-{}-{}-{}'.format(target_branch, base_distro, build_hash)
        set_output('restore_prev', restore_prev)

    else:
        # PR builds.  Do not embed the current commit in the hash name, load the most recent build
        # scripts, and fall back to the most recent version of the build script from the last week
        # or anything if that isn't found.
        key = 'bc-{}-{}-{}-{}'.format(target_branch, base_distro, build_hash, '{hash}')
        set_output('key', key)

        restore = 'bc-{}-{}-{}'.format(target_branch, base_distro, build_hash)
        set_output('restore', restore)

        if len(lines):
            restore_prev = 'bc-{}-{}-{}'.format(target_branch, base_distro, lines[0])
        else:
            restore_prev = 'bc-{}-{}-'.format(target_branch, base_distro)
        set_output('restore_prev', restore_prev)


if __name__ == '__main__':
    main()
