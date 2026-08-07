"""NLT: shared dfuse/IL test helpers.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import subprocess  # nosec
from os.path import join

from .base import NLTestFail
from .client import assert_file_size, assert_file_size_fd, run_daos_cmd


def run_tests(dfuse):
    """Run some tests"""
    # pylint: disable=consider-using-with
    path = dfuse.dir

    fname = join(path, 'test_file3')

    rc = subprocess.run(['dd', 'if=/dev/zero', 'bs=16k', 'count=64',  # nosec
                         f'of={join(path, "dd_file")}'],
                        check=True)
    print(rc)
    ofd = open(fname, 'w')
    ofd.write('hello')
    print(os.fstat(ofd.fileno()))
    ofd.flush()
    print(os.stat(fname))
    assert_file_size(ofd, 5)
    ofd.truncate(0)
    assert_file_size(ofd, 0)
    ofd.truncate(1024 * 1024)
    assert_file_size(ofd, 1024 * 1024)
    ofd.truncate(0)
    ofd.seek(0)
    ofd.write('simple file contents\n')
    ofd.flush()
    assert_file_size(ofd, 21)
    print(os.fstat(ofd.fileno()))
    ofd.close()
    ofd = os.open(fname, os.O_TRUNC)
    assert_file_size_fd(ofd, 0)
    os.close(ofd)
    symlink_name = join(path, 'symlink_src')
    symlink_dest = 'missing_dest'
    os.symlink(symlink_dest, symlink_name)
    assert symlink_dest == os.readlink(symlink_name)

    # Note that this doesn't test dfs because fuse will do a
    # lookup to check if the file exists rather than just trying
    # to create it.
    fname = join(path, 'test_file5')
    fd = os.open(fname, os.O_CREAT | os.O_EXCL)
    os.close(fd)
    try:
        fd = os.open(fname, os.O_CREAT | os.O_EXCL)
        os.close(fd)
        assert False
    except FileExistsError:
        pass
    os.unlink(fname)


def stat_and_check(dfuse, pre_stat):
    """Check that dfuse started"""
    post_stat = os.stat(dfuse.dir)
    if pre_stat.st_dev == post_stat.st_dev:
        raise NLTestFail('Device # unchanged')
    if post_stat.st_ino != 1:
        raise NLTestFail('Unexpected inode number')


def check_no_file(dfuse):
    """Check that a non-existent file doesn't exist"""
    path = join(dfuse.dir, 'no-file')
    try:
        os.stat(path)
        raise NLTestFail(f'file exists: {path}')
    except FileNotFoundError:
        pass


def create_and_read_via_il(dfuse, path):
    """Create file in dir, write to and read through the interception library"""
    fname = join(path, 'test_file')
    with open(fname, 'w') as ofd:
        ofd.write('hello ')
        ofd.write('world\n')
        ofd.flush()
        assert_file_size(ofd, 12)
        print(os.fstat(ofd.fileno()))

        # Replace Python snippet with dd to guarantee read()
        dfuse.il_cmd([
            'dd',
            f'if={fname}',
            'of=/dev/null',
            'bs=4096',
            'iflag=fullblock',
            'status=none'
        ], check_write=False, check_fstat=False)


def run_container_query(conf, path):
    """Query a path to extract container information"""
    cmd = ['container', 'query', '--path', path]

    rc = run_daos_cmd(conf, cmd)

    assert rc.returncode == 0

    print(rc)
    output = rc.stdout.decode('utf-8')
    for line in output.splitlines():
        print(line)
