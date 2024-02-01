"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import stat

from dfuse_test_base import DfuseTestBase
from run_utils import run_remote

OUTER = """#!/bin/bash

set -uex

[ -d out_dir ] && rm -rf out_dir
[ -d e_out_dir ] && rm -rf e_out_dir
mkdir out_dir

cd out_dir

.././bash_fd_inner.sh

cd -

grep . out_dir/*

mkdir e_out_dir
echo first file > e_out_dir/out_file
echo first file >> e_out_dir/out_file

echo second file > e_out_dir/other_file
echo five >> e_out_dir/other_file
echo six >> e_out_dir/other_file

diff --new-file --recursive out_dir e_out_dir
"""

INNER = """#!/bin/bash

set -ue

echo Hello, about to perform some bash I/O testing similar to "configure" scripts.

# Open file for read/write access.
exec 3<>out_file
echo first file >&3

ls -l /proc/$$/fd

# Open file in /proc and hold it open.
exec 4</proc/self/maps

# Open file for write access.
exec 5>other_file
echo second file >&5

# Duplicate fd.
exec 6>&5
echo five >&6

ls -l /proc/$$/fd

# Close fds as output file descriptors.
exec 4>&-
exec 5>&-
echo six >&6
exec 6>&-

echo first file >&3
exec 3>&-

ls -l /proc/$$/fd

exit 0
"""


class DFuseFdTest(DfuseTestBase):
    """Base FdTest test class.

    :avocado: recursive
    """

    def run_bashfd(self, il_lib=None):
        """Run a shell script which opens and writes to files.

        This attempts to replicate the way that configure scripts manipulate fds in bash.

        Args:
            il_lib (str, optional): interception library to run with. Defaults to None
        """

        if il_lib is not None:
            lib_path = os.path.join(self.prefix, "lib64", il_lib)
            env_str = f"export LD_PRELOAD={lib_path}; "
        else:
            env_str = ""

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)
        self.start_dfuse(self.hostlist_clients, pool, container)

        fuse_root_dir = self.dfuse.mount_dir.value

        with open(os.path.join(fuse_root_dir, "bash_fd_inner.sh"), "w") as fd:
            fd.write(INNER)

        os.chmod(os.path.join(fuse_root_dir, "bash_fd_inner.sh"), stat.S_IXUSR | stat.S_IRUSR)

        with open(os.path.join(fuse_root_dir, "bash_fd_outer.sh"), "w") as fd:
            fd.write(OUTER)

        os.chmod(os.path.join(fuse_root_dir, "bash_fd_outer.sh"), stat.S_IXUSR | stat.S_IRUSR)

        cmd = f"cd {fuse_root_dir}; ./bash_fd_outer.sh"

        result = run_remote(self.log, self.hostlist_clients, env_str + cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

    def test_bashfd(self):
        """

        Test Description:
            Test a typical I/O pattern for bash based configure scripts.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfs
        :avocado: tags=DFuseFdTest,test_bashfd
        """
        self.run_bashfd()

    def test_bashfd_ioil(self):
        """

        Test Description:
            Test a typical I/O pattern for bash based configure scripts.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,il,dfs
        :avocado: tags=DFuseFdTest,test_bashfd_ioil
        """
        self.run_bashfd(il_lib="libioil.so")

    def test_bashfd_pil4dfs(self):
        """

        Test Description:
            Test a typical I/O pattern for bash based configure scripts.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pil4dfs,dfs
        :avocado: tags=DFuseFdTest,test_bashfd_pil4dfs
        """
        self.run_bashfd(il_lib="libpil4dfs.so")
