"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import stat

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from host_utils import get_local_host
from run_utils import run_remote

SCRIPT = """#!/bin/bash

dir_name="dir00"
file_name="file00"

mkdir ${dir_name}
echo "Hello" > ${dir_name}/${file_name}
# The dir should be cached now.

rm -rf ${dir_name}
# The dir is removed by a child process. Current bash is not aware of it.
# The cached dir record is out of date.

mkdir ${dir_name}

echo "Hello" > ${dir_name}/${file_name}
# With out of date cached dir record, a file with wrong path is created.

cat ${dir_name}/${file_name}
# The file previously created has wrong path. "cat" should fail.
"""


class DFuseBashdcacheTest(TestWithServers):
    # pylint: disable=wrong-spelling-in-docstring
    """Base "Bashdcache" test class.

    :avocado: recursive
    """

    def test_bash_dcache_pil4dfs(self):
        # pylint: disable=wrong-spelling-in-docstring
        """Run a shell script which creates dir and file, then removes them and recreates.

        This attempts to replicate the way that configure scripts repeating creating & removing
        files under "conftest.dir" in bash.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfs,dfuse,pil4dfs
        :avocado: tags=DFuseBashdcacheTest,test_bash_dcache_pil4dfs
        """

        lib_path = os.path.join(self.prefix, "lib64", "libpil4dfs.so")
        env_str = f"export LD_PRELOAD={lib_path}; export D_IL_NO_BYPASS=1; "

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)
        dfuse_hosts = get_local_host()
        dfuse = get_dfuse(self, dfuse_hosts)
        start_dfuse(self, dfuse, pool, container)
        fuse_root_dir = dfuse.mount_dir.value

        with open(os.path.join(fuse_root_dir, "sh_dcache.sh"), "w", encoding="utf-8") as fd:
            fd.write(SCRIPT)

        os.chmod(os.path.join(fuse_root_dir, "sh_dcache.sh"), stat.S_IXUSR | stat.S_IRUSR)

        cmd = f"cd {fuse_root_dir}; ./sh_dcache.sh"

        result = run_remote(self.log, dfuse_hosts, env_str + cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')
        if result.output[0].stdout[0][:5] != "Hello":
            self.fail(f'"{cmd}" failed on {result.failed_hosts}. Unexpected output.')

        # Turn on directory caching in bash
        env_str = env_str + "export D_IL_NO_DCACHE_BASH=0; "

        result = run_remote(self.log, dfuse_hosts, env_str + cmd)
        if result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')
