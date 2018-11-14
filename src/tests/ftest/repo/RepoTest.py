#!/usr/bin/python

import os
import time

from apricot import Test
from avocado import main
from avocado.utils import process
from avocado.utils import git

class RepoTest(Test):
    """
    Tests DAOS repository and build process.

    :avocado: recursive
    """

    def test_git(self):
        """
        :avocado: tags=git,build
        """
        repoloc = self.params.get("repoloc",'/files/','rubbish')

        repo = git.GitRepoHelper("ssh://skirvan@review.whamcloud.com:29418/daos/daos_m",
                                 'master', None, None, repoloc, None)
        repo.execute()
        repo.git_cmd('submodule init')
        repo.git_cmd('submodule update')


    def test_build(self):
        """
        :avocado: tags=git,build
        """
        cmd = 'cd ' + repoloc + '; scons --build-deps=yes install'
        process.system(cmd, shell=True)
