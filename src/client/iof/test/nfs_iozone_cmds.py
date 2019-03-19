#!/usr/bin/env python3
"""
Please see nfs_iozone_main.yml for prerequisite setup details.

This program:

nfs_iozone_cmds.py

is executed on the exportfs node within the execStrategy of:

nfs_iozone_cmds.yml

which was evaluated in the execStrategy of:

nfs_iozone_main.yml

as part of the launching of test_runner:

python3.4 test_runner config=scripts/nfs_iozone_main.cfg \
          scripts/nfs_iozone_main.yml ;

in the directory:

install/Linux/TESTING/scripts/

from within the root of the iof repository.

nfs_iozone_cmds.py needs to exist in the following directories:

install/Linux/TESTING/scripts/

from within the root of the iof repository.

nfs_iozone_cmds.py does the following:

1) Extract the IP address of export node.
2) Extract the IP address of mount node.
3) Create exportfs dir on export node.
4) Export the export dir on export node.
5) Create the mount dir on the mount node using paramiko.
6) Mount the export dir from export node on to the mount dir on the mount
   node using paramiko.
7) Perform the following iozone operations in the mount dir, on mount node
   using paramiko:
   iozone write
   iozone read
   iozone random (read & write)
8) Unmount mount dir on mount node using paramiko.
9) Delete mount dir on mount node using paramiko.
10) Unexport exportfs dir on exportfs node.
11) Delete exportfs dir on exportfs node.
"""

import os
import getpass
import sys
import subprocess
import logging
# pylint: disable=import-error
import NodeControlRunner
# pylint: enable=import-error

# pylint: disable=too-many-instance-attributes
# pylint: disable=protected-access

class Nfs_Iozone_Cmds():
    """Example test_runner python script class."""

    def __init__(self, test_info=None, log_base_path=None):
        self.test_info = test_info
        self.info = test_info.info
        self.log_dir_base = log_base_path
        self.logger = logging.getLogger("TestRunnerLogger")

        self.user = getpass.getuser()
        self.uid = os.getuid()
        self.gid = os.getgid()

        # NFS related variables.

        self.export_node = self.test_info.get_test_info('defaultENV',
                                                        'IOF_TEST_ION')
        self.export_ip_addr = None
        self.export_dir = None
        self.export_fs = None

        self.mount_node = self.test_info.get_test_info('defaultENV',
                                                       'IOF_TEST_CN')
        self.mount_ip_addr = None
        self.mount_dir = None
        self.mount_fs = None

        self.net_interface = self.test_info.get_test_info('defaultENV',
                                                          'OFI_INTERFACE')

        self.ncr = NodeControlRunner.NodeControlRunner(self.log_dir_base,
                                                       self.test_info)

        self.test_root_dir = "/tmp"
        self.test_dir = "iozone"
        self.test_file = "test.bin"
        self.test_file_size = "512K"
        self.test_timeout = 30 * 10 # Time in seconds.

        ld_library_path = self.test_info.get_defaultENV('LD_LIBRARY_PATH')
        self.test_cmd = "LD_LIBRARY_PATH={!s}".format(ld_library_path)
        self.test_cmd = "{!s} /usr/bin/iozone".format(self.test_cmd)

        self.test_cmd_args = "-t 1 -c -e -w -r 4k -+n -s"
        self.test_cmd_args = "{!s} {!s}".format(
            self.test_cmd_args, self.test_file_size.lower())

        self.test_file_content = "72" # Octet value populated into test file.


    def useLogDir(self, log_path):
        """create the log directory name"""
        self.log_dir_base = log_path


    def log_cmd_ret_local(self, cmd, s):
        """Used to execute cmd on local host."""

        self.logger.info("%s cmd = %s\n", s, cmd)
        ret = subprocess.getoutput(cmd)
        self.logger.info("%s ret = %s\n", s, ret)

        return ret


    def log_cmd_ret_remote(self, cmd, s):
        """Used to execute cmd on remote host."""

        self.logger.info("%s cmd = %s\n", s, cmd)
        ret = self.ncr.paramiko_execute_remote_cmd(
            self.mount_node, cmd, "", True, self.test_timeout)
        ret = ret.data
        self.logger.info("%s ret = %s\n", s, ret)

        return ret


    def init(self):
        """Init all NFS related variables."""

        c_name = self.__class__.__name__
        f_name = sys._getframe().f_code.co_name

        # Init export node and mount node ip addresses.

        # Network interface command.

        cmd = "/usr/sbin/ip -oneline -4 addr show dev"
        cmd = "{!s} {!s}".format(cmd, self.net_interface)

        # Export Node

        self.logger.info("export_node (%s): %s.%s(): cmd = %s",
                         self.export_node, c_name, f_name, cmd)

        ret = subprocess.getoutput(cmd)

        self.logger.info("export_node (%s): %s.%s(): ret = %s\n",
                         self.export_node, c_name, f_name, ret)

        ret = ret.split(" ")[6].split(".")
        ret[3] = ret[3].split("/")[0]
        self.export_ip_addr = ".".join(ret)

        # Mount Node

        self.logger.info("mount_node (%s): %s.%s(): cmd = %s",
                         self.mount_node, c_name, f_name, cmd)

        ret = self.ncr.paramiko_execute_remote_cmd(self.mount_node, cmd, "",
                                                   True, self.test_timeout)
        ret = ret.data

        self.logger.info("mount_node (%s): %s.%s(): ret = %s",
                         self.mount_node, c_name, f_name, ret)

        ret = ret.split(" ")[6].split(".")
        ret[3] = ret[3].split("/")[0]
        self.mount_ip_addr = ".".join(ret)

        # Export

        self.export_dir = "{!s}/{!s}/{!s}/export".format(self.test_root_dir,
                                                         self.user,
                                                         self.test_dir)
        self.export_fs = "{!s}:{!s}".format(self.mount_ip_addr,
                                            self.export_dir)
        # Mount

        self.mount_dir = "{!s}/{!s}/{!s}/mount".format(self.test_root_dir,
                                                       self.user,
                                                       self.test_dir)
        self.mount_fs = "{!s}:{!s}".format(self.export_ip_addr,
                                           self.export_dir)
        # Display init values.

        self.logger.info("user            = %s", self.user)
        self.logger.info("net_interface   = %s", self.net_interface)
        self.logger.info("export_node     = %s", self.export_node)
        self.logger.info("export_ip_addr  = %s", self.export_ip_addr)
        self.logger.info("export_dir      = %s", self.export_dir)
        self.logger.info("export_fs       = %s", self.export_fs)
        self.logger.info("mount_node      = %s", self.mount_node)
        self.logger.info("mount_ip_addr   = %s", self.mount_ip_addr)
        self.logger.info("mount_dir       = %s", self.mount_dir)
        self.logger.info("mount_fs        = %s", self.mount_fs)
        self.logger.info("\n")
        return 0

    def test_file_create(self):
        """Create test file in export node."""

        c_name = self.__class__.__name__
        f_name = sys._getframe().f_code.co_name

        s = "export_node ({!s}): {!s}.{!s}():".format(
            self.export_node, c_name, f_name)

        # Create iozone binary test file in the exported dir on export node.
        # NOTE: mount() invocation is a prerequisite for this
        #       method.
        # The iozone process on the mount node will use this test file.

        cmd = "/usr/bin/dd"
        cmd = "{!s} status=none".format(cmd)
        cmd = "{!s} bs={!s} count=1".format(cmd, self.test_file_size.upper())
        cmd = "{!s} if=/dev/zero".format(cmd)
        cmd = "{!s} | /usr/bin/tr".format(cmd)
        cmd = "{!s} \'\\0\' \'\\{!s}\' >".format(cmd, self.test_file_content)
        cmd = "{!s} {!s}/{!s}".format(cmd, self.export_dir, self.test_file)

        ret = self.log_cmd_ret_local(cmd, s)

        # Verify existence of test file.

        cmd = "/usr/bin/ls -l"
        cmd = "{!s} {!s}/{!s}".format(cmd, self.export_dir, self.test_file)

        ret = self.log_cmd_ret_local(cmd, s)

        if ret == "":
            return 1
        return 0

    def export(self):
        """Export NFS filesystem on export node."""

        c_name = self.__class__.__name__
        f_name = sys._getframe().f_code.co_name

        s = "export_node ({!s}): {!s}.{!s}():".format(
            self.export_node, c_name, f_name)

        # Start NFS daemon.

        cmd = "sudo systemctl start nfs"

        ret = self.log_cmd_ret_local(cmd, s)

        # Make export dir on export node (i.e. local node.)

        cmd = "mkdir -pv {!s}".format(self.export_dir)

        ret = self.log_cmd_ret_local(cmd, s)

        # NFS-export export_fs on export node.

        cmd = "sudo /usr/sbin/exportfs"
        cmd = "{!s} -v -i -o rw,".format(cmd)
        cmd = "{!s}anonuid={!s},anongid={!s}".format(cmd, self.uid, self.gid)
        cmd = "{!s} {!s}".format(cmd, self.export_fs)

        ret = self.log_cmd_ret_local(cmd, s)

        # Show exported filesystems.

        cmd = "/usr/sbin/showmount -e"

        ret = self.log_cmd_ret_local(cmd, s)

        if ret == "":
            return 1
        return 0

    def mount(self):
        """Mount exported NFS filesystem on mount node."""

        c_name = self.__class__.__name__
        f_name = sys._getframe().f_code.co_name

        s = "mount_node ({!s}): {!s}.{!s}():".format(
            self.mount_node, c_name, f_name)

        # Make mount dir on mount node.

        cmd = "/usr/bin/mkdir -pv {!s}".format(self.mount_dir)

        ret = self.log_cmd_ret_remote(cmd, s)

        # Mount export_fs from export node onto (mount dir) mount-point on mount
        # node.

        cmd = "sudo /usr/bin/mount -v {!s} {!s}".format(self.mount_fs,
                                                        self.mount_dir)

        ret = self.log_cmd_ret_remote(cmd, s)

        # Verify mount-point.

        cmd = "/usr/bin/mount | grep tmp | grep {!s}".format(self.user)

        ret = self.log_cmd_ret_remote(cmd, s)

        if ret == "":
            return 1
        return 0

    def read(self):
        """IOZone read test."""

        c_name = self.__class__.__name__
        f_name = sys._getframe().f_code.co_name

        s = "mount_node ({!s}): {!s}.{!s}():".format(
            self.mount_node, c_name, f_name)

        # Create test file.

        self.test_file_create()

        # Launch iozone cmd on mount node.

        cmd = "{!s} -i 1 {!s}".format(self.test_cmd, self.test_cmd_args)
        cmd = "{!s} -F {!s}/{!s}".format(cmd,
                                         self.mount_dir,
                                         self.test_file)

        ret = self.log_cmd_ret_remote(cmd, s)

        if ret == "":
            return 1
        return 0

    def write(self):
        """IOZone write test."""

        c_name = self.__class__.__name__
        f_name = sys._getframe().f_code.co_name

        s = "mount_node ({!s}): {!s}.{!s}():".format(
            self.mount_node, c_name, f_name)
        # Launch iozone cmd on mount node.

        cmd = "{!s} -i 0 {!s}".format(self.test_cmd, self.test_cmd_args)
        cmd = "{!s} -F {!s}/{!s}".format(cmd,
                                         self.mount_dir,
                                         self.test_file)

        ret = self.log_cmd_ret_remote(cmd, s)

        if ret == "":
            return 1
        return 0

    def random(self):
        """IOZone random (read/write) test."""

        c_name = self.__class__.__name__
        f_name = sys._getframe().f_code.co_name

        s = "mount_node ({!s}): {!s}.{!s}():".format(
            self.mount_node, c_name, f_name)

        # Create test file.

        self.test_file_create()

        # Launch iozone cmd on mount node.

        cmd = "{!s} -i 2 {!s}".format(self.test_cmd, self.test_cmd_args)
        cmd = "{!s} -F {!s}/{!s}".format(cmd,
                                         self.mount_dir,
                                         self.test_file)

        ret = self.log_cmd_ret_remote(cmd, s)

        if ret == "":
            return 1
        return 0

    def unmount(self):
        """Unmount NFS mounted filesystem on mount node."""

        c_name = self.__class__.__name__
        f_name = sys._getframe().f_code.co_name

        s = "mount_node ({!s}): {!s}.{!s}():".format(
            self.mount_node, c_name, f_name)

        # Unmount (mount dir) mount-point on mount node.

        cmd = "sudo /usr/bin/umount -v {!s}".format(self.mount_dir)

        self.log_cmd_ret_remote(cmd, s)

        # Remove mount dir on mount node.

        cmd = "/usr/bin/rm -rfv {!s}/{!s}".format(self.test_root_dir,
                                                  self.user)

        self.log_cmd_ret_remote(cmd, s)
        return 0

    def unexport(self):
        """ Unexport NFS filesystem on export node."""

        c_name = self.__class__.__name__
        f_name = sys._getframe().f_code.co_name

        s = "export_node ({!s}): {!s}.{!s}():".format(
            self.export_node, c_name, f_name)

        # Unexport export_fs on export node.

        cmd = "sudo /usr/sbin/exportfs -v -u {!s}".format(self.export_fs)

        self.log_cmd_ret_local(cmd, s)

        # Remove export dir on export node.

        cmd = "/usr/bin/rm -rfv {!s}/{!s}".format(self.test_root_dir,
                                                  self.user)

        self.log_cmd_ret_local(cmd, s)

        # Stop NFS daemon.

        cmd = "sudo systemctl stop nfs"

        self.log_cmd_ret_local(cmd, s)
        return 0

# pylint: enable=protected-access
# pylint: enable=too-many-instance-attributes
