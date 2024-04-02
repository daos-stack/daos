"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

import general_utils
from ClusterShell.NodeSet import NodeSet
from dfuse_test_base import DfuseTestBase
from exception_utils import CommandFailure


class Cmd(DfuseTestBase):
    """Base Cmd test class.

    :avocado: recursive
    """

    def run_bashcmd(self, il_lib=None, compatible_mode=False):
        """Jira ID: DAOS-3508.

        Use cases:
            Following list of bash commands have been incorporated
            as part of this test: mkdir, touch, ls, chmod, rm, dd, stat,
            cp, cmp, mv, rmdir.
              Create a directory.
              Create a file under that directory.
              List the created file.
              Remove the file.
              Write a file to the dfuse mounted location using dd.
              List the written file to verify if it's create.
              Verify the file created is of right size as desired.
              Copy the file
              Compare the copied file with original to verify the
              content is same.
              Remove copied file.
              Rename file
              Verify renamed file exist using list.
              Verify dfuse support for '.'
              Verify dfuse support for '..'
              Remove renamed file
              Remove a directory
              and more
        """
        dir_name = self.params.get("dirname", '/run/bashcmd/*')
        file_name1 = self.params.get("filename1", '/run/bashcmd/*')
        file_name2 = self.params.get("filename2", '/run/bashcmd/*')
        dd_count = self.params.get("dd_count", '/run/bashcmd/*')
        dd_blocksize = self.params.get("dd_blocksize", '/run/bashcmd/*')
        pool_count = self.params.get("pool_count", '/run/pool/*')
        cont_count = self.params.get("cont_count", '/run/container/*')

        if il_lib is not None:
            # no need to run multiple pools and containers with interception lib loaded
            pool_count = 1
            cont_count = 1
            lib_path = os.path.join(self.prefix, 'lib64', il_lib)
            if compatible_mode:
                env_str = f"export LD_PRELOAD={lib_path}; export D_IL_COMPATIBLE=1; "
            else:
                env_str = f"export LD_PRELOAD={lib_path}; "
        else:
            env_str = ""

        # Create a pool if one does not already exist.
        for _ in range(pool_count):
            self.add_pool(connect=False)
            # perform test for multiple containers.
            for count in range(cont_count):
                self.add_container(self.pool)
                mount_dir = f"/tmp/{self.pool.uuid}_daos_dfuse{count}"
                self.start_dfuse(
                    self.hostlist_clients, self.pool, self.container, mount_dir=mount_dir)
                if il_lib is not None:
                    # unmount dfuse and mount again with caching disabled
                    self.dfuse.unmount(tries=1)
                    self.dfuse.update_params(disable_caching=True)
                    self.dfuse.update_params(disable_wb_cache=True)
                    self.dfuse.run()

                fuse_root_dir = self.dfuse.mount_dir.value
                abs_dir_path = os.path.join(fuse_root_dir, dir_name)
                abs_file_path1 = os.path.join(abs_dir_path, file_name1)
                abs_file_path2 = os.path.join(abs_dir_path, file_name2)

                with open(os.path.join(fuse_root_dir, 'src.c'), 'w') as fd:
                    fd.write('#include <stdio.h>\n\nint main(void) {\nprintf("Hello World!");\n}\n')
                link_name = os.path.join(fuse_root_dir, 'link_c')

                with open(os.path.join(fuse_root_dir, 'src_a.c'), 'w') as fd:
                    fd.write('#include <stdio.h>\n\nvoid fun_a(void) {\nprintf("fun_a()");\n}\n')
                with open(os.path.join(fuse_root_dir, 'src_b.c'), 'w') as fd:
                    fd.write('#include <stdio.h>\n\nvoid fun_b(void) {\nprintf("fun_b()");\n}\n')
                # list of commands to be executed.
                commands = [f"mkdir -p {abs_dir_path}",
                            f"touch {abs_file_path1}",
                            f"ls -a {abs_file_path1}",
                            f"rm {abs_file_path1}",
                            f"dd if=/dev/zero of={abs_file_path1} count={dd_count}"
                            f" bs={dd_blocksize}",
                            f"ls -al {abs_file_path1}",
                            f"filesize=$(stat -c%s '{abs_file_path1}');"
                            f"if (( filesize != {dd_count}*{dd_blocksize} )); then exit 1; fi",
                            f"cp -r {abs_file_path1} {abs_file_path2}",
                            f"diff {abs_file_path1} {abs_file_path2}",
                            f"cmp --silent {abs_file_path1} {abs_file_path2}",
                            f"rm {abs_file_path2}",
                            f"mv {abs_file_path1} {abs_file_path2}",
                            f"ls -al {abs_file_path2}",
                            f"ls -al {abs_dir_path}/.",
                            f"ls -al {abs_dir_path}/..",
                            f"rm {abs_file_path2}",
                            f"rmdir {abs_dir_path}",
                            f"wc -l {fuse_root_dir}/src.c",
                            f"xxd {fuse_root_dir}/src.c",
                            f'ln -s src.c {link_name}',
                            f'readlink {link_name}',
                            f'realpath {link_name}',
                            f'head {fuse_root_dir}/src.c',
                            f'tail {fuse_root_dir}/src.c',
                            # f'more {fuse_root_dir}/src.c', # more hangs over ssh somehow
                            f'dos2unix {fuse_root_dir}/src.c',
                            f"gcc -o {fuse_root_dir}/output {fuse_root_dir}/src.c",
                            f'size {fuse_root_dir}/output',
                            f'readelf -s {fuse_root_dir}/output',
                            f'strip -s {fuse_root_dir}/output',
                            f"g++ -o {fuse_root_dir}/output {fuse_root_dir}/src.c",
                            f"gcc -c -o {fuse_root_dir}/obj_a.o {fuse_root_dir}/src_a.c",
                            f"gcc -c -o {fuse_root_dir}/obj_b.o {fuse_root_dir}/src_b.c",
                            f"ar -rc {fuse_root_dir}/lib.a {fuse_root_dir}/obj_a.o "
                            f"{fuse_root_dir}/obj_b.o",
                            f"objdump -d {fuse_root_dir}/obj_a.o",
                            f"grep print {fuse_root_dir}/src.c",
                            "awk '{print $1}' " + fuse_root_dir + '/src.c',
                            f"base64 {fuse_root_dir}/src.c",
                            f"md5sum {fuse_root_dir}/src.c",
                            f"cksum {fuse_root_dir}/src.c",
                            f"bzip2 -z {fuse_root_dir}/lib.a",
                            f"chmod u-r {fuse_root_dir}/lib.a.bz2",
                            'fio --readwrite=randwrite --name=test --size="2M" --directory '\
                            f'{fuse_root_dir}/ --bs=1M --numjobs="4" --ioengine=psync '\
                            '--group_reporting --exitall_on_error --continue_on_error=none',
                            'fio --readwrite=randwrite --name=test --size="2M" --directory '\
                            f'{fuse_root_dir}/ --bs=1M --numjobs="1" --ioengine=libaio --iodepth=4'\
                            '--group_reporting --exitall_on_error --continue_on_error=none',
                            f"curl \"https://www.google.com\" -o {fuse_root_dir}/download.html"]
                for cmd in commands:
                    try:
                        # execute bash cmds
                        ret_code = general_utils.pcmd(
                            self.hostlist_clients, env_str + cmd, timeout=30)
                        if 0 not in ret_code:
                            error_hosts = NodeSet(
                                ",".join(
                                    [str(node_set) for code, node_set in
                                     list(ret_code.items()) if code != 0]))
                            raise CommandFailure(
                                f"Error running '{cmd}' on the following hosts: {error_hosts}")
                    # report error if any command fails
                    except CommandFailure as error:
                        self.log.error("BashCmd Test Failed: %s",
                                       str(error))
                        self.fail("Test was expected to pass but "
                                  "it failed.\n")

                # stop dfuse
                self.stop_dfuse()
                # destroy container
                self.container.destroy()
            # destroy pool
            self.pool.destroy()

    def test_bashcmd(self):
        """

        Test Description:
            Purpose of this test is to mount different mount points of dfuse
            for different container and pool sizes and perform basic bash
            commands.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dfuse
        :avocado: tags=Cmd,test_bashcmd
        """
        self.run_bashcmd()

    def test_bashcmd_ioil(self):
        """

        Test Description:
            Purpose of this test is to mount different mount points of dfuse
            for different container and pool sizes and perform basic bash
            commands.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dfuse,il
        :avocado: tags=Cmd,test_bashcmd_ioil
        """
        self.run_bashcmd(il_lib="libioil.so")

    def test_bashcmd_pil4dfs(self):
        """

        Test Description:
            Purpose of this test is to mount different mount points of dfuse
            for different container and pool sizes and perform basic bash
            commands.

        :avocado: tags=all
        :avocado: tags=hw,medium
        :avocado: tags=dfuse,pil4dfs
        :avocado: tags=Cmd,test_bashcmd_pil4dfs
        """
        self.run_bashcmd(il_lib="libpil4dfs.so")
