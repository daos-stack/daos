"""
Some useful test classes inherited from avocado.Test
"""

import os
import subprocess
import json

from avocado import Test as avocadoTest
from avocado import skip

import ServerUtils
import WriteHostFile
from daos_api import DaosContext, DaosLog

def skipForTicket(ticket):
    return skip("Skipping until {} is fixed.".format(ticket))

class Test(avocadoTest):
    '''
    Basic Test class

    :avocado: recursive
    '''
    # pylint: disable=line-too-long
    '''
    members:
        {'_Test__running': True,
         '_Test__fail_class': None,
         '_stderr_file': '/home/brian/daos/daos/src/tests/ftest/avocado/job-results/job-2018-11-06T18.10-4d5d5a4/test-results/1-test_example.py:MyTest.test/stderr',
         'paused': False,
         '_cleanups': [],
         '_stdout_file': '/home/brian/daos/daos/src/tests/ftest/avocado/job-results/job-2018-11-06T18.10-4d5d5a4/test-results/1-test_example.py:MyTest.test/stdout',
         '_Test__logfile': '/home/brian/daos/daos/src/tests/ftest/avocado/job-results/job-2018-11-06T18.10-4d5d5a4/test-results/1-test_example.py:MyTest.test/debug.log',
         '_testMethodDoc': '\n        :avocado: tags=all\n        ',
         '_Test__sysinfodir': '/home/brian/daos/daos/src/tests/ftest/avocado/job-results/job-2018-11-06T18.10-4d5d5a4/test-results/1-test_example.py:MyTest.test/sysinfo',
         'time_start': 1541545807.658046,
         '_Test__params': <AvocadoParams *: * ([]),0: / (['//']),1: r ([]),2: u ([]),3: n ([])>,
         'file_handler': <logging.FileHandler object at 0x7f342537a250>,
         '_expected_stdout_file': '/home/brian/daos/daos/src/tests/ftest/test_example.py.data/stdout.expected',
         '_Test__traceback': None,
         '_Test__runner_queue': <multiprocessing.queues.SimpleQueue object at 0x7f342537a710>,
         '_Test__fail_reason': None,
         'workdir': '/var/tmp/avocado_5z0_yb/1-test_example.py_MyTest.test',
         '_Test__name': '1-test_example.py:MyTest.test',
         '_Test__status': None,
         '_Test__logdir': '/home/brian/daos/daos/src/tests/ftest/avocado/job-results/job-2018-11-06T18.10-4d5d5a4/test-results/1-test_example.py:MyTest.test',
         '_ssh_logfile': '/home/brian/daos/daos/src/tests/ftest/avocado/job-results/job-2018-11-06T18.10-4d5d5a4/test-results/1-test_example.py:MyTest.test/remote.log',
         'srcdir': '/var/tmp/avocado_5z0_yb/1-test_example.py_MyTest.test/src',
         'paused_msg': '',
         '_Test__sysinfo_logger': <avocado.core.sysinfo.SysInfo object at 0x7f34253983d0>,
         '_Test__outputdir': '/home/brian/daos/daos/src/tests/ftest/avocado/job-results/job-2018-11-06T18.10-4d5d5a4/test-results/1-test_example.py:MyTest.test/data',
         '_testMethodName': 'test',
         '_resultForDoCleanups': None,
         '_type_equality_funcs': {<type 'set'>: 'assertSetEqual',
         <type 'dict'>: 'assertDictEqual',
         <type 'unicode'>: 'assertMultiLineEqual',
         <type 'list'>: 'assertListEqual',
         <type 'frozenset'>: 'assertSetEqual',
         <type 'tuple'>: 'assertTupleEqual'},
         '_logging_handlers': {'avocado.test.stderr': <logging.FileHandler object at 0x7f342431d8d0>,
         'avocado.test.stdout': <logging.FileHandler object at 0x7f342431d850>,
         'paramiko': <logging.FileHandler object at 0x7f342431d950>},
         '_Test__sysinfo_enabled': <avocado.core.sysinfo.SysInfo object at 0x7f34253ccc10>,
         '_Test__log': <logging.Logger object at 0x7f34303e3ad0>,
         '_Test__log_warn_used': False,
         'timeout': None,
         '_expected_stderr_file': '/home/brian/daos/daos/src/tests/ftest/test_example.py.data/stderr.expected',
         '_Test__job': <avocado.core.job.Job object at 0x7f34253c4350>}
    '''
    # pylint: enable=line-too-long

    def __init__(self, *args, **kwargs):
        super(Test, self).__init__(*args, **kwargs)
        # set a default timeout of 1 minute
        # tests that want longer should set a timeout in their .yaml file
        # all tests should set a timeout and 60 seconds will enforce it
        if not self.timeout:
            self.timeout = 60

        item_list = self._Test__logdir.split('/')
        for item_num in range(len(item_list)):
            if item_list[item_num] == 'job-results':
                self.job_id = item_list[item_num + 1]
                break

        self.log.info("Job-ID: {}".format(self.job_id))
        self.log.info("Test PID: %s", os.getpid())

        self.basepath = None
        self.orterun = None
        self.tmp = None
        self.server_group = None
        self.daosctl = None
        # TODO: harmonize the various CONTEXT, Context, context, etc.
        self.CONTEXT = None
        self.Context = None
        self.context = None
        # TODO: harmonize the various POOL, pool, Pool, etc.
        self.POOL = None
        self.pool = None
        self.CONTAINER = None
        self.container = None
        self.hostlist = None
        self.hostlist_servers = None
        self.hostfile_servers = None
        self.d_log = None
        self.uri_file = None

        # try collecting this in sysinfo
        ## display process tree before starting (to see what previous left
        ## behind
        #self.log.info("Process tree at start:\n" + \
        #    subprocess.check_output(['ps', 'axf',]))

    def setUp(self):
        self.log.info("setUp() executed from Apricot.Test")

    def tearDown(self):
        self.log.info("tearDown() executed from Apricot.Test")

    def cancelForTicket(self, ticket):
        ''' Skip a test due to a ticket needing to be completed '''
        return self.cancel("Skipping until {} is fixed.".format(ticket))

class TestWithoutServers(Test):
    '''
    Run tests without DAOS servers

    :avocado: recursive
    '''
    def setUp(self):
        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as build_vars:
            build_paths = json.load(build_vars)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        self.tmp = build_paths['PREFIX'] + '/tmp'
        self.daos_test = self.basepath + '/install/bin/daos_test'
        self.orterun = self.basepath + '/install/bin/orterun'
        self.daosctl = self.basepath + '/install/bin/daosctl'

        # this causes a timeout in setUp() to not call tearDown()
        #context = DaosContext(build_paths['PREFIX'] + '/lib/')
        # and this needs the above so we need to comment it out also
        #self.d_log = DaosLog(context)
        # ditto
        #self.d_log.debug("Starting test {}".
                         #format(self._Test__name)) # pylint: disable=no-member
        super(TestWithoutServers, self).setUp()

    #def tearDown(self):
        # see above.  this needs DaosContext() also
        #self.d_log.debug("Ending test {}".
        #                 format(self._Test__name)) # pylint: disable=no-member
        super(TestWithoutServers, self).tearDown()

class TestWithServers(TestWithoutServers):
    '''
    Run tests with DAOS servers

    :avocado: recursive
    '''

    def setUp(self):
        super(TestWithServers, self).setUp()

        self.server_group = self.params.get("server_group", '/server/', 'daos_server')
        self.hostlist = self.params.get("test_machines", '/run/hosts/*')

        self.hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.workdir)

        # clean /mnt/daos/* on all servers
        for server in self.hostlist:
            subprocess.call(["ssh", server, "rm -rf /mnt/daos/*"])

        ServerUtils.runServer(self.hostfile, self.server_group, self.basepath)

    def tearDown(self):

        ServerUtils.stopServer(hosts=self.hostlist)

        # pylint: disable=no-member
        test_name = self._Test__name.str_filesystem()
        # pylint: enable=no-member
        # collect up a debug log so that we have a separate one for each
        # subtest
        try:
            logfile = os.environ['D_LOG_FILE']
            dirname, filename = os.path.split(logfile)
            new_logfile = os.path.join(
                dirname,
                "{}_{}".format(test_name, filename)) # pylint: disable=no-member
            # rename on each of the servers
            cmd = '[ ! -f \"{0}\" ] && exit 0 || ' \
                  '    mv \"{0}\" \"{1}\"'.format(logfile, new_logfile)
            for host in self.hostlist:
                subprocess.check_call(['ssh', host, cmd])
        except KeyError:
            pass

        super(TestWithServers, self).tearDown()
