#!/usr/bin/python

import os
import time

from avocado import Test
from avocado import main
from avocado.utils import process
from avocado.utils import git

class RepoTest(Test):
    """
    Tests DAOS repository and build process.

    avocado: tags=git,build
    """
    def setUp(self):
        # not used at present
        pass

        #self.runServer()

    def tearDown(self):
        # not used at present
        pass


    def test_git(self):
        repoloc = self.params.get("repoloc",'/files/','rubbish')

        repo = git.GitRepoHelper("ssh://skirvan@review.whamcloud.com:29418/daos/daos_m",
                                 'master', None, None, repoloc, None)
        repo.execute()
        repo.git_cmd('submodule init')
        repo.git_cmd('submodule update')


    def test_build(self):
        cmd = 'cd ' + repoloc + '; scons --build-deps=yes install'
        process.system(cmd, shell=True)

if __name__ == "__main__":
    main()

