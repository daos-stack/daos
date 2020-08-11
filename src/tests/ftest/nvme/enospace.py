#!/usr/bin/python
'''
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
import time
import threading
import uuid

from nvme_utils import ServerFillUp
from avocado.core.exceptions import TestFail
from general_utils import get_log_file, run_task
from daos_utils import DaosCommand
#from apricot import skipForTicket
from mpio_utils import MpioUtils
from job_manager_utils import Mpirun
from ior_utils import IorCommand, IorMetrics
from command_utils_base import CommandFailure

try:
    # python 3.x
    import queue
except ImportError:
    # python 2.7
    import Queue as queue

class NvmeEnospace(ServerFillUp):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate DER_NOSPACE for SCM and NVMe
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a NvmeEnospace object."""
        super(NvmeEnospace, self).__init__(*args, **kwargs)
        self.daos_cmd = None

    def setUp(self):
        super(NvmeEnospace, self).setUp()

        # initialize daos command
        self.daos_cmd = DaosCommand(self.bin)
        self.create_pool_max_size()
        self.der_nospace_count = 0
        self.other_errors_count = 0

    def der_enspace_log_count(self):
        """
        Function to count the DER_NOSPACE and other ERR in client log.
        """
        #Get the Client side Error from client_log file.
        output = []
        cmd = 'cat {} | grep ERR'.format(get_log_file(self.client_log))
        task = run_task(self.hostlist_clients, cmd)
        for buf, _nodes in task.iter_buffers():
            output = str(buf).split('\n')

        for line in output:
            if 'ERR' in line:
                if 'DER_NOSPACE' in line:
                    self.der_nospace_count += 1
                else:
                    self.other_errors_count += 1

    def verify_enspace_log(self, der_nospace_err_count):
        """
        Function to verify there are no other error except DER_NOSPACE
        in client log and also DER_NOSPACE count is higher.

        args:
            expected_err_count(int): Expected DER_NOSPACE count from client log.
        """
        #Get the DER_NOSPACE and other error count from log
        self.der_enspace_log_count()

        #Check there are no other errors in log file
        if self.other_errors_count > 0:
            self.fail('Found other errors, count {} in client log {}'
                      .format(self.other_errors_count, self.client_log))
        #Check the DER_NOSPACE error count is higher if not test will FAIL
        if self.der_nospace_count < der_nospace_err_count:
            self.fail('Expected DER_NOSPACE should be > {} and Found {}'
                      .format(der_nospace_err_count, self.der_nospace_count))

    def delete_all_containers(self):
        """
        Delete all the containers.
        """
        #List all the container
        kwargs = {"pool": self.pool.uuid, "svc": self.pool.svc_ranks}
        continers = (self.daos_cmd.get_output("pool_list_cont", **kwargs))
        #Destroy all the containers
        for _cont in continers:
            kwargs["cont"] = _cont
            self.daos_cmd.container_destroy(**kwargs)

    def ior_bg_thread(self, results):
        """Start IOR Background thread, This will write small data set and
        keep reading it in loop until it fails or main program exit.

        Args:
            results (queue): queue for returning thread results
        """
        con_uuid = str(uuid.uuid4())
        mpio_util = MpioUtils()
        if mpio_util.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test: Mpich not installed")

        # Define the IOR Command and use the parameter from yaml file.
        ior_bg_cmd = IorCommand()
        ior_bg_cmd.get_params(self)
        ior_bg_cmd.set_daos_params(self.server_group, self.pool)
        ior_bg_cmd.daos_oclass.update(self.ior_cmd.daos_oclass.value)
        ior_bg_cmd.api.update(self.ior_cmd.api.value)
        ior_bg_cmd.transfer_size.update(self.ior_cmd.transfer_size.value)
        ior_bg_cmd.block_size.update(self.ior_cmd.block_size.value)
        ior_bg_cmd.flags.update(self.ior_cmd.flags.value)

        # Define the job manager for the IOR command
        manager = Mpirun(ior_bg_cmd, mpitype="mpich")
        manager.job.daos_cont.update(con_uuid)
        env = ior_bg_cmd.get_default_env(str(manager))
        manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        manager.assign_processes(1)
        manager.assign_environment(env, True)
        print('----Run IOR in Background-------')
        # run IOR Write Command
        try:
            manager.run()
        except (CommandFailure, TestFail) as _error:
            results.put("FAIL")
            return

        # run IOR Read Command in loop
        ior_bg_cmd.flags.update(self.ior_read_flags)
        while True:
            try:
                manager.run()
            except (CommandFailure, TestFail) as _error:
                results.put("FAIL")
                break

    def run_enospace_foreground(self):
        """
        Function to run test and validate DER_ENOSPACE and expected storage size
        """
        #Fill 75% of SCM pool, Aggregation is Enabled so one point of time, data
        #will be aggregated and moved from SCM to NVMe.
        print('Starting main IOR load')
        self.start_ior_load(storage='SCM', precent=75)
        print(self.pool.pool_percentage_used())

        #Fill 75% more of SCM pool,Aggregation is Enabled so NVMe space will be
        #filled
        self.start_ior_load(storage='SCM', precent=75)
        print(self.pool.pool_percentage_used())

        #Fill 60% more of SCM pool, now NVMe will be Full so data will not be
        #moved to NVMe but it will start filling SCM. SCM size will be going to
        #full and this command expected to fail with DER_NOSPACE
        try:
            self.start_ior_load(storage='SCM', precent=60)
            self.fail('This test suppose to FAIL because of DER_NOSPACE'
                      'but it got Passed')
        except TestFail as _error:
            self.log.info('Test expected to fail because of DER_NOSPACE')

        #Display the pool%
        print(self.pool.pool_percentage_used())

        #verify the DER_NO_SAPCE error count is expected and no other Error in
        #client log
        self.verify_enspace_log(self.der_nospace_count)

        #Check both NVMe and SCM are full.
        pool_usage = self.pool.pool_percentage_used()
        #NVM should be 99% full if not test will fail.
        if pool_usage['nvme'] > 1:
            self.fail('Pool NVMe used percentage should be < 1%, instead {}'.
                      format(pool_usage['nvme']))
        #For SCM some % space used for system so it wont be 100% full.
        if pool_usage['scm'] > 25:
            self.fail('Pool SCM used percentage should be < 20%, instead {}'.
                      format(pool_usage['scm']))

    def run_enospace_with_bg_job(self):
        """
        Function to run test and validate DER_ENOSPACE and expected storage
        size. Single IOR job will run in background while space is filling.
        """
        #Get the initial DER_ENOSPACE count
        self.der_enspace_log_count()

        # Start the IOR Background thread which will write small data set and
        # read in loop, until storage space is full.
        out_queue = queue.Queue()
        job = threading.Thread(target=self.ior_bg_thread,
                               kwargs={"results": out_queue})
        job.daemon = True
        job.start()

        #Run IOR in Foreground
        self.run_enospace_foreground()
        # Verify the background job queue and make sure no FAIL for any IOR run
        while not self.out_queue.empty():
            if self.out_queue.get() == "FAIL":
                self.fail("One of the Background IOR job failed")

    #@skipForTicket("DAOS-4846")
    def test_enospace_lazy_with_bg(self):
        """Jira ID: DAOS-4756.

        Test Description: IO gets DER_NOSPACE when SCM and NVMe is full with
                          default (lazy) Aggregation mode.

        Use Case: This tests will create the pool and fill 75% of SCM size which
                  will trigger the aggregation because of space pressure,
                  next fill 75% more which should fill NVMe. Try to fill 60%
                  more and now SCM size will be full too.
                  verify that last IO fails with DER_NOSPACE and SCM/NVMe pool
                  capacity is full.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=der_enospace,enospc_lazy,enospc_lazy_bg
        """
        print(self.pool.pool_percentage_used())

        #Run IOR to fill the pool.
        self.run_enospace_with_bg_job()

    #@skipForTicket("DAOS-4846")
    def test_enospace_lazy_with_fg(self):
        """Jira ID: DAOS-4756.

        Test Description: Fill up the system (default aggregation mode) and
                          delete all containers in loop, which should release
                          the space.

        Use Case: This tests will create the pool and fill 75% of SCM size which
                  will trigger the aggregation because of space pressure,
                  next fill 75% more which should fill NVMe. Try to fill 60%
                  more and now SCM size will be full too.
                  verify that last IO fails with DER_NOSPACE and SCM/NVMe pool
                  capacity is full. Delete all the containers.
                  Do this in loop for 10 times and verify space is released.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=der_enospace,enospc_lazy,enospc_lazy_fg
        """
        print(self.pool.pool_percentage_used())

        #Repeat the test in loop.
        for _loop in range(10):
            #Run IOR to fill the pool.
            self.run_enospace_foreground()
            #Delete all the containers
            self.delete_all_containers()

    #@skipForTicket("DAOS-4846")
    def test_enospace_time_with_bg(self):
        """Jira ID: DAOS-4756.

        Test Description: IO gets DER_NOSPACE when SCM is full and it release
                          the size when container destroy with Aggregation
                          set on time mode.

        Use Case: This tests will create the pool. Set Aggregation mode to Time.
                  Fill 75% of SCM size which will trigger the aggregation
                  because mode set to time, next fill 75% more which will fill
                  up NVMe. Try to fill 60% more and now SCM size will be full
                  too. Verify last IO fails with DER_NOSPACE and SCM/NVMe
                  pool capacity is full.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=der_enospace,enospc_time,enospc_time_bg
        """
        print(self.pool.pool_percentage_used())

        # Enabled TIme mode for Aggregation.
        self.pool.set_property("reclaim", "time")

        #Run IOR to fill the pool.
        self.run_enospace_with_bg_job()

    #@skipForTicket("DAOS-4846")
    def test_enospace_time_with_fg(self):
        """Jira ID: DAOS-4756.

        Test Description: Fill up the system (time aggregation mode) and
                          delete all containers in loop, which should release
                          the space.

        Use Case: This tests will create the pool. Set Aggregation mode to Time.
                  Fill 75% of SCM size which will trigger the aggregation
                  because mode set to time, next fill 75% more which will fill
                  up NVMe. Try to fill 60% more and now SCM size will be full
                  too. Verify last IO fails with DER_NOSPACE and SCM/NVMe
                  pool capacity is full. Delete all the containers.
                  Do this in loop for 10 times and verify space is released.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=der_enospace,enospc_time,enospc_time_fg
        """
        print(self.pool.pool_percentage_used())

        # Enabled TIme mode for Aggregation.
        self.pool.set_property("reclaim", "time")

        #Repeat the test in loop.
        for _loop in range(10):
            #Run IOR to fill the pool.
            self.run_enospace_with_bg_job()
            #Delete all the containers
            self.delete_all_containers()

    #@skipForTicket("DAOS-4846")
    #@skipForTicket("DAOS-5430")
    def test_performance_storage_full(self):
        """Jira ID: DAOS-4756.

        Test Description: Verify IO Read performance when pool size is full.

        Use Case: This tests will create the pool. Run small set of IOR as
                  baseline.Start IOR with < 4K which will start filling SCM
                  and trigger aggregation and start fill up NVMe.
                  Check the IOR baseline read number and make sure it's close
                  to the number ran prior system storage was full.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=der_enospace,enospc_performance
        """
        #Write the IOR Baseline and get the Read BW for later comparison.
        print(self.pool.pool_percentage_used())
        #Write First
        self.start_ior_load(storage='SCM', precent=1)
        #Read the baseline data set
        self.start_ior_load(storage='SCM', operation='Read', precent=1)
        max_mib_baseline = float(self.ior_matrix[0][int(IorMetrics.Max_MiB)])
        baseline_cont_uuid = self.ior_cmd.daos_cont.value
        print("IOR Baseline Read MiB {}".format(max_mib_baseline))

        #Run IOR to fill the pool.
        self.run_enospace_with_bg_job()

        #Read the same container which was written at the beginning.
        self.ior_cmd.daos_cont.value = baseline_cont_uuid
        self.start_ior_load(storage='SCM', operation='Read', precent=1)
        max_mib_latest = float(self.ior_matrix[0][int(IorMetrics.Max_MiB)])
        print("IOR Latest Read MiB {}".format(max_mib_latest))

        #Check if latest IOR read performance is in Tolerance of 5%, when
        #Storage space is full.
        if abs(max_mib_baseline-max_mib_latest) > (max_mib_baseline/100 * 5):
            self.fail('Latest IOR read performance is not under 5% Tolerance'
                      ' Baseline Read MiB = {} and latest IOR Read MiB = {}'
                      .format(max_mib_latest, max_mib_baseline))

    #@skipForTicket("DAOS-4846")
    def test_enospace_no_aggregation(self):
        """Jira ID: DAOS-4756.

        Test Description: IO gets DER_NOSPACE when SCM is full and it release
                          the size when container destroy with Aggregation
                          disabled.

        Use Case: This tests will create the pool and disable aggregation. Fill
                  75% of SCM size which should work, next try fill 10% more
                  which should fail with DER_NOSPACE. Destroy the container
                  and validate the Pool SCM free size is close to full (> 95%).
                  Do this in loop ~10 times and verify the DER_NOSPACE and SCM
                  free size after container destroy.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=der_enospace,enospc_no_aggregation
        """
        # pylint: disable=attribute-defined-outside-init
        # pylint: disable=too-many-branches
        print(self.pool.pool_percentage_used())

        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")

        #Get the initial DER_ENOSPACE count
        self.der_enspace_log_count()

        #Repeat the test in loop.
        for _loop in range(10):
            #Fill 75% of SCM pool
            self.start_ior_load(storage='SCM', precent=75)

            print(self.pool.pool_percentage_used())

            try:
                #Fill 10% more to SCM ,which should Fail because no SCM space
                self.start_ior_load(storage='SCM', precent=10)
                self.fail('This test suppose to fail because of DER_NOSPACE'
                          'but it got Passed')
            except TestFail as _error:
                self.log.info('Expected to fail because of DER_NOSPACE')

            #Verify DER_NO_SAPCE error count is expected and no other Error
            #in client log.
            self.verify_enspace_log(self.der_nospace_count)

            #Delete all the containers
            self.delete_all_containers()

            #Get the pool usage
            pool_usage = self.pool.pool_percentage_used()
            #Delay to release the SCM size.
            time.sleep(60)
            print(pool_usage)
            #SCM pool size should be released (some still be used for system)
            #Pool SCM free % should not be less than 95%
            if pool_usage['scm'] < 95:
                self.fail('SCM pool used percentage should be < 95, instead {}'.
                          format(pool_usage['scm']))

        #Run last IO
        self.start_ior_load(storage='SCM', precent=1)
