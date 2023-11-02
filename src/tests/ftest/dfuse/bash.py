"""
  (C) Copyright 2020-2023 Intel Corporation.

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

                abs_dir_path = os.path.join(
                    self.dfuse.mount_dir.value, dir_name)
                abs_file_path1 = os.path.join(abs_dir_path, file_name1)
                abs_file_path2 = os.path.join(abs_dir_path, file_name2)

                src_c_name = os.path.join(self.dfuse.mount_dir.value, 'src.c')
                with open(src_c_name, 'w') as fd:
                    fd.write('#include <stdio.h>\n\nint main(void) {\nprintf("Hello World!");\n}\n')
                output_c = os.path.join(self.dfuse.mount_dir.value, 'output_c')
                link_name = os.path.join(self.dfuse.mount_dir.value, 'link_c')

                src_f_name = os.path.join(self.dfuse.mount_dir.value, 'src.f')
                with open(src_f_name, 'w') as fd:
                    fd.write('      program test\n      write(*,*) "Hello"\n      end program\n')
                output_f = os.path.join(self.dfuse.mount_dir.value, 'output_f')

                src_a_name = os.path.join(self.dfuse.mount_dir.value, 'src_a.c')
                with open(src_a_name, 'w') as fd:
                    fd.write('#include <stdio.h>\n\nvoid fun_a(void) {\nprintf("fun_a()");\n}\n')
                src_b_name = os.path.join(self.dfuse.mount_dir.value, 'src_b.c')
                with open(src_b_name, 'w') as fd:
                    fd.write('#include <stdio.h>\n\nvoid fun_b(void) {\nprintf("fun_b()");\n}\n')
                obj_a = os.path.join(self.dfuse.mount_dir.value, 'src_a.o')
                obj_b = os.path.join(self.dfuse.mount_dir.value, 'src_b.o')
                lib_ab = os.path.join(self.dfuse.mount_dir.value, 'lib.a')
                download = os.path.join(self.dfuse.mount_dir.value, 'download.html')
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
                            f"wc -l {src_c_name}",
                            f"xxd {src_c_name}",
                            f'ln -s src.c {link_name}',
                            f'readlink {link_name}',
                            f'realpath {link_name}',
                            f'head {src_c_name}',
                            f'tail {src_c_name}',
                            # f'more {src_c_name}', # more hangs over ssh somehow
                            f'dos2unix {src_c_name}',
                            f"gcc -o {output_c} {src_c_name}",
                            f'size {output_c}',
                            f'readelf -s {output_c}',
                            f'strip -s {output_c}',
                            f"g++ -o {output_c} {src_c_name}",
                            f"gfortran -o {output_f} {src_f_name}",
                            f"gcc -c -o {obj_a} {src_a_name}",
                            f"gcc -c -o {obj_b} {src_b_name}",
                            f"ar -rc {lib_ab} {obj_a} {obj_b}",
                            f"objdump -d {obj_a}",
                            f"grep print {src_a_name}",
                            "awk '{print $1}' " + src_f_name,
                            f"base64 {src_f_name}",
                            f"md5sum {src_f_name}",
                            f"cksum {src_f_name}",
                            f"bzip2 -z {lib_ab}",
                            f"chmod u-r {lib_ab}.bz2",
                            f"wget \"https://www.google.com\" -O {download}",
                            f"curl \"https://www.google.com\" -o {download}"]
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
        :avocado: tags=dfuse
        :avocado: tags=Cmd,test_bashcmd,test_bashcmd_ioil
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
        :avocado: tags=dfuse
        :avocado: tags=Cmd,test_bashcmd,test_bashcmd_pil4dfs
        """
        self.run_bashcmd(il_lib="libpil4dfs.so")
