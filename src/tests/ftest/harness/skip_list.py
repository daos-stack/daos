"""
  (C) Copyright 2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import errno
from apricot import Test


class TestHarnessSkipsBase(Test):
    """Base class for test classes below."""

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super().__init__(*args, **kwargs)
        self.commit_fixes_file = os.path.join(os.sep, 'tmp', 'commit_fixes')

    def setUp(self):
        """Use our own CI-skip-list-master to test to run these tests."""
        self.cancel_file = os.path.join(os.sep, 'tmp', 'skip_list')
        with open(self.cancel_file, 'w') as skip_handle:
            skip_handle.write(
                '''[['DAOS-0000', 'test_method_name', 'test_case_1']]
[['DAOS-0000', 'test_method_name', 'test_case_2']]|b4a912f
[['DAOS-0000', 'test_method_name', 'test_case_3']]|abcd123
[['DAOS-9999', 'test_method_name', 'test_case_4']]
[['DAOS-9999', 'test_method_name', 'test_case_5']]|b4a912f
[['DAOS-9999', 'test_method_name', 'test_case_6']]|abcd123''')
        self.cancel_file = self.cancel_file

        # create a temporary commit_fixes file
        try:
            os.rename(self.commit_fixes_file, self.commit_fixes_file + '.orig')
        except OSError as excpt:
            if excpt.errno == errno.ENOENT:
                pass
            else:
                self.fail("Could not rename {0}"
                          "{{,.orig}}: {1}".format(self.commit_fixes_file,
                                                   excpt))
        try:
            with open(self.commit_fixes_file, 'w') as cf_handle:
                cf_handle.write("DAOS-9999 test: Fixing DAOS-9999")
        except Exception as excpt:  # pylint: disable=broad-except
            self.fail("Could not create {0}: "
                      "{1}".format(self.commit_fixes_file, excpt))

        super().setUp()

    def tearDown(self):
        """Put back the original commit_fixes file."""
        try:
            os.unlink(self.commit_fixes_file)
        except Exception as excpt:  # pylint: disable=broad-except
            self.fail("Could not remove {0}: "
                      "{1}".format(self.commit_fixes_file, excpt))
        try:
            os.rename(self.commit_fixes_file + '.orig', self.commit_fixes_file)
        except OSError as excpt:
            if excpt.errno == errno.ENOENT:
                pass
        except Exception as excpt:  # pylint: disable=broad-except
            self.fail("Could not rename {0}{{.orig,}}: "
                      "{1}".format(self.commit_fixes_file, excpt))

        super().tearDown()


class TestHarnessSkipsSkipped(TestHarnessSkipsBase):
    """Test cases where the test should be skipped.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestHarnessSkipsSkipped object."""
        super().__init__(*args, **kwargs)
        self.cancelled = False

    def cancel(self, message=None):
        # pylint: disable=arguments-renamed
        """Override Avocado Test.cancel()."""
        self.log.info("Test correctly called cancel(%s)", message)
        self.cancelled = True

    def test_case_1(self):
        """Test w/o a fix in this commit w/o a committed fix.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,test_skips
        :avocado: tags=TestHarnessSkipsSkipped,test_case_1
        """
        if not self.cancelled:
            self.fail("This test was not skipped as it should have been")

    def test_case_3(self):
        """Test w/o a fix in the commit w/ a committed fix not in the code base.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,test_skips
        :avocado: tags=TestHarnessSkipsSkipped,test_case_3
        """
        if not self.cancelled:
            self.fail("This test was not skipped as it should have been")


class TestHarnessSkipsRun(TestHarnessSkipsBase):
    """Test cases where the test should run.

    :avocado: recursive
    """

    def cancel(self, message=None):
        """Override Test.cancel()."""
        # pylint: disable=unused-argument
        # pylint: disable=arguments-renamed
        self.fail('This test should not be skipped')

    def test_case_2(self):
        """Test w/o a fix in this commit w/ a committed fix in this code base.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,test_skips
        :avocado: tags=TestHarnessSkipsRun,test_case_2
        """

    def test_case_4(self):
        """Test w/ a fix in this commit w/o a committed fix.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,test_skips
        :avocado: tags=TestHarnessSkipsRun,test_case_4
        """

    def test_case_5(self):
        """Test w/ a fix in this commit w/ a committed fix in this code base.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,test_skips
        :avocado: tags=TestHarnessSkipsRun,test_case_5
        """

    def test_case_6(self):
        """Test w/ a fix in the commit w/ a committed fix not in the code base.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,test_skips
        :avocado: tags=TestHarnessSkipsRun,test_case_6
        """
