"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import grp
import os
import pwd
import re

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from general_utils import DaosTestError
from run_utils import issue_command
from security_test_base import acl_entry


class ContSecurityTestBase(TestWithServers):
    """Container security test cases.

    Test Class Description:
        Test methods to verify the Container security with acl by
        using daos tool.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ContSecurityTestBase object."""
        super().__init__(*args, **kwargs)
        self.dmg = None
        self.user_uid = os.geteuid()
        self.user_gid = os.getegid()
        self.current_user = pwd.getpwuid(self.user_uid)[0]
        self.current_group = grp.getgrgid(self.user_uid)[0]

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.co_prop = self.params.get("container_properties", "/run/container/*")
        self.dmg = self.get_dmg_command()

    def create_container_with_daos(self, pool, acl_type=None, acl_file=None, cont_type=None):
        """Create a container with the daos tool.

        Args:
            pool (TestPool): Pool object.
            acl_type (str, optional): valid or invalid.
            acl_file (str, optional): acl file
            cont_type (str, optional): container type

        Returns:
            TestContainer: the new container

        """
        expected_acl_types = [None, "valid", "invalid"]

        if acl_file is None:
            if acl_type not in expected_acl_types:
                self.fail("Invalid '{}' acl type passed.".format(acl_type))
            if acl_type:
                acl_file = os.path.join(self.tmp, "acl_{}.txt".format(acl_type))

        try:
            return self.get_container(pool, type=cont_type, acl_file=acl_file)
        except TestFail as error:
            if acl_type != "invalid":
                raise DaosTestError("Could not create expected container ") from error
        return None

    def get_container_acl_list(self, container, verbose=False, outfile=None):
        """Get daos container acl list by daos container get-acl.

        Args:
            container (TestContainer): the container.
            verbose (bool, optional): Verbose mode.
            outfile (str, optional): Write ACL to file

        Return:
            cont_permission_list: daos container acl list.

        """
        result = container.get_acl(verbose, outfile)

        cont_permission_list = []
        for line in result.stdout_text.splitlines():
            if not line.startswith("A:"):
                continue
            if line.startswith("A::"):
                found_user = re.search(r"A::(.+)@:(.*)", line)
                if found_user:
                    cont_permission_list.append(line)
            elif line.startswith("A:G:"):
                found_group = re.search(r"A:G:(.+)@:(.*)", line)
                if found_group:
                    cont_permission_list.append(line)
        return cont_permission_list

    def compare_acl_lists(self, get_acl_list, expected_list):
        """Compare two permission lists.

        Args:
            get_acl_list (str list): list of permissions obtained by get-acl
            expected_list (str list): list of expected permissions

        Returns:
            True or False if both permission lists are identical or not

        """
        self.log.info("    ===> get-acl ACL:  %s", get_acl_list)
        self.log.info("    ===> Expected ACL: %s", expected_list)
        return sorted(get_acl_list) == sorted(expected_list)

    def get_base_acl_entries(self, test_user):
        """Get container acl entries per cont enforcement order for test_user.

        Args:
            test_user (str): test user.

        Returns (list str):
            List of base container acl entries for the test_user.

        """
        if test_user == "OWNER":
            base_acl_entries = [
                acl_entry("user", "OWNER", ""),
                acl_entry("user", self.current_user, ""),
                acl_entry("group", "GROUP", "rwcdtTaAo"),
                acl_entry("group", self.current_group, "rwcdtTaAo"),
                acl_entry("user", "EVERYONE", "rwcdtTaAo")]
        elif test_user == "user":
            base_acl_entries = [
                "",
                acl_entry("user", self.current_user, ""),
                acl_entry("group", "GROUP", "rwcdtTaAo"),
                acl_entry("group", self.current_group, ""),
                acl_entry("user", "EVERYONE", "rwcdtTaAo")]
        elif test_user == "group":
            base_acl_entries = [
                "",
                "",
                acl_entry("group", "GROUP", ""),
                acl_entry("group", self.current_group, ""),
                acl_entry("user", "EVERYONE", "rwcdtTaAo")]
        elif test_user == "GROUP":
            base_acl_entries = [
                "",
                "",
                "",
                acl_entry("group", self.current_group, ""),
                acl_entry("user", "EVERYONE", "rwcdtTaAo")]
        elif test_user == "EVERYONE":
            base_acl_entries = [
                "",
                "",
                "",
                "",
                acl_entry("user", "EVERYONE", "")]
        else:
            base_acl_entries = ["", "", "", "", ""]
        return base_acl_entries

    def cleanup(self, types):
        """Remove all temporal acl files created during the test.

        Args:
            types (list): types of acl files [valid, invalid]

        """
        for typ in types:
            get_acl_file = "acl_{}.txt".format(typ)
            file_name = os.path.join(self.tmp, get_acl_file)
            if not issue_command(self.log, f"rm -r {file_name}").passed:
                raise DaosTestError(f"Error removing {file_name}")

    def error_handling(self, results, err_msg):
        """Handle errors when test fails and when command unexpectedly passes.

        Args:
            results (CmdResult): object containing stdout, stderr and
                exit status.
            err_msg (str): error message string to look for in stderr.

        Returns:
            list: list of test errors encountered.

        """
        test_errs = []
        if results.exit_status == 0:
            test_errs.append("{} passed unexpectedly: {}".format(
                results.command, results.stdout_text))
        elif results.exit_status == 1:
            # REMOVE BELOW IF Once DAOS-5635 is resolved
            if results.stdout_text and err_msg in results.stdout_text:
                self.log.info("Found expected error %s", results.stdout_text)
            # REMOVE ABOVE IF Once DAOS-5635 is resolved
            elif results.stderr_text and err_msg in results.stderr_text:
                self.log.info("Found expected error %s", results.stderr_text)
            else:
                self.fail("{} seems to have failed with \
                    unexpected error: {}".format(results.command, results))
        return test_errs

    def acl_file_diff(self, container, prev_acl, flag=True):
        """Compare current content of acl-file with helper function.

        If provided  prev_acl file information is different from current acl
        file information test will fail if flag=True. If flag=False, test will
        fail in the case that the acl contents are found to have no difference.

        Args:
            container (TestContainer): container for which to compare acl file.
            prev_acl (list): list of acl entries within acl-file.
                Defaults to True.
            flag (bool, optional): if True, test will fail when acl-file
                contents are different, else test will fail when acl-file
                contents are same. Defaults to True.

        """
        current_acl = self.get_container_acl_list(container)
        if self.compare_acl_lists(prev_acl, current_acl) != flag:
            self.fail("Previous ACL:\n{} \nPost command ACL:\n{}".format(prev_acl, current_acl))
