"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import stat

from dfuse_test_base import DfuseTestBase
from run_utils import run_remote

SCRIPT = """#!/bin/bash

#!/bin/bash

dir_name="dir00"
file_name="file00"

rm -rf ${dir_name}
mkdir ${dir_name}
echo "Hello" > ${dir_name}/${file_name}
rm -rf ${dir_name}

mkdir ${dir_name}
echo "Hello" > ${dir_name}/${file_name}
cat ${dir_name}/${file_name}
"""


class DFuseBashdcacheTest(DfuseTestBase):
    """Base Bashdcache test class.

    :avocado: recursive
    """

    def run_bash_dcache_pil4dfs(self):
        """Run a shell script which creates dir and file, then removes them and recreates.

        This attempts to replicate the way that configure scripts repeating creating & removing
        files under conftest.dir in bash.
        """

        lib_path = os.path.join(self.prefix, "lib64", "libpil4dfs.so")
        env_str = f"export LD_PRELOAD={lib_path}; "

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)
        self.start_dfuse(self.hostlist_clients, pool, container)

        fuse_root_dir = self.dfuse.mount_dir.value

        with open(os.path.join(fuse_root_dir, "sh_dcache.sh"), "w") as fd:
            fd.write(SCRIPT)

        os.chmod(os.path.join(fuse_root_dir, "sh_dcache.sh"), stat.S_IXUSR | stat.S_IRUSR)

        cmd = f"cd {fuse_root_dir}; ./sh_dcache.sh"

        result = run_remote(self.log, self.hostlist_clients, env_str + cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        # Turn on dcache in bash
        env_str = env_str + "export D_IL_NO_DCACHE_BASH=0; "

        result = run_remote(self.log, self.hostlist_clients, env_str + cmd)
        if result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

    def test_bash_dcache_pil4dfs(self):
        """

        Test Description:
            Test a typical I/O pattern accessing conftest.dir in configure scripts.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pil4dfs,dfs
        :avocado: tags=DFuseFdTest,test_bashdcache_pil4dfs
        """
        self.run_bash_dcache_pil4dfs()
