#!/usr/bin/python3

"""Helper module to choose build/cache keys to use for github actions"""

import os
import sys
import subprocess #nosec

BUILD_FILES = ['site_scons',
               'utils/build.config',
               'SConstruct',
               '.github/workflows/landing-builds.yml'
               '.dockerignore',
               'ci/gha_helper.y']

COMMIT_CMD = ['git', 'rev-parse', '--short', 'HEAD']


def set_output(key, value):
    """ Set a key-value pair in github actions metadata"""

    print('::set-output name={}::{}'.format(key, value))

def main():
    """Parse git histrory to load caches for GHA"""

    single = '--single' in sys.argv

    base_distro = os.getenv('BASE_DISTRO', None)

    cmd = ['git', 'rev-list', '--abbrev-commit']

    if single:
        cmd.append('--max-count=1')
    else:
        cmd.extend(['--max-count=2', '--since=7.days'])
    cmd.extend(['HEAD', '--'])
    cmd.extend(BUILD_FILES)

    if base_distro:
        cmd.append('utils/docker/Dockerfile.{}'.format(base_distro))
    else:
        cmd.append('utils/docker')
        base_distro = ''

    rc = subprocess.run(cmd, check=True, capture_output=True)
    commits = rc.stdout.decode('utf-8').strip()

    if single:
        build_hash = commits

    else:
        lines = commits.splitlines()
        build_hash = lines.pop(0)

    if single:
        # Landings builds, embed the current commit in the hash name, load either the exact commit
        # or the most recent build with the same build scripts.
        rc = subprocess.run(COMMIT_CMD, check=True, capture_output=True)
        commit_hash = rc.stdout.decode('utf-8').strip()

        key = 'bc-{}-{}-{}-{}'.format(base_distro, build_hash, commit_hash, '{hash}')
        set_output('key', key)

        restore = 'bc-{}-{}-{}'.format(base_distro, build_hash, commit_hash)
        set_output('restore', restore)

        restore_prev = 'bc-{}-{}'.format(base_distro, build_hash)
        set_output('restore_prev', restore_prev)

    else:
        # PR builds.  Do not embed the current commit in the hash name, load the most recent build
        # scripts, and fall back to the most recent version of the build script from the last week
        # or anything if that isn't found.
        key = 'bc-{}-{}-{}'.format(base_distro, build_hash, '{hash}')
        set_output('key', key)

        restore = 'bc-{}-{}'.format(base_distro, build_hash)
        set_output('restore', restore)

        if len(lines):
            restore_prev = 'bc-{}-{}'.format(base_distro, lines[0])
        else:
            restore_prev = 'bc-{}-'.format(base_distro)
        set_output('restore_prev', restore_prev)

if __name__ == '__main__':
    main()
