#!/usr/bin/python
"""
(C) Copyright 2019 Intel Corporation.

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
provided in Contract No. 8F-30005.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""

import os
import time
from apricot import TestWithServers
from ior_utils import IorCommand
import slurm_utils
from test_utils import TestPool, TestContainer
from ClusterShell.NodeSet import NodeSet
from general_utils import pcmd
import socket
import threading
from avocado.core.exceptions import TestFail
from avocado.utils import process

H_LOCK = threading.Lock()


class SoakTestError(Exception):
    """Soak exception class."""


class Soak(TestWithServers):
    """Execute DAOS Soak test cases.

    :avocado: recursive
    Args:
        TestWithServers (AvocadoTest): Unit Test test cases
    There are currently two types of soak tests.
        1) smoke - runs each specified cmdline (job spec) for a single
           iteration. The smoke test is to verify the environment is
           configured properly before running the longer soaks
        2) 1 hour - this will run a defined set of jobs and continue to submit
           the jobs until the time has expired.

    The tests also use an IOR that is compiled with MPICH and is built with
    both the DAOS and MPI-IO drivers.

    """

    def job_done(self, args):
        """Call this function when a job is done.

        Args:
            args (list):handle --which job, i.e. the job ID,
                        state  --string indicating job completion status
        """
        self.soak_results[args["handle"]] = args["state"]

    def add_pools(self, pool_names):
        """Add a pool that the various tests use for storage.

        Args:
            pool_names: list of pool name from yaml file
                        /run/<test_params>/poollist/*
        """
        for pool_name in pool_names:
            path = "".join(["/run/", pool_name, "/*"])
            # Create a pool
            self.pool.append(TestPool(self.context, self.log))
            self.pool[-1].namespace = path
            self.pool[-1].get_params(self)
            self.pool[-1].create()
            self.log.info("Valid Pool UUID is %s", self.pool[-1].uuid)

            # Check that the pool was created
            self.assertTrue(
                self.pool[-1].check_files(self.hostlist_servers),
                "Pool data not detected on servers")

    def destroy_pool(self, pool):
        """Destroy the specified pool - TO DO."""
        pass

    def remote_copy(self, hostlist, remote_dir, local_dir):
        """Copy files from remote dir to local dir.

        Args:
                hostlist (list): list of remote nodes
                remote_dir (str): remote directory of files
                local_dir (str): local directory

        Raises:
            SoakTestError: if there is an error with the remote copy

        """
        this_host = socket.gethostname()
        result = pcmd(
            NodeSet.fromlist(hostlist),
            "if [ ! -z '$(ls -A {0})' ]; then "
            "scp -p -r {0}/ \"{1}:'{2}/'\" && rm -rf {0}/*; fi".format(
                remote_dir, this_host, local_dir),
            verbose=False)
        if len(result) > 1 or 0 not in result:
            raise SoakTestError(
                "Error executing remote copy: {}".format(
                    ", ".join(
                        [str(result[key]) for key in result if key != 0])))

    def is_harasser(self, harasser):
        """Check if harasser is defined in yaml.

        Args:
            harasser (list): list of harassers to launch

        Returns: bool

        """
        return self.h_list and harasser in self.h_list

    def launch_harassers(self, harassers, pools):
        """Launch any harasser tests if defined in yaml.

        Args:
            harasser (list): list of harassers to launch
            pools (TestPool): pool obj

        """
        job = []
        # Launch harasser after one complete pass
        for harasser in harassers:
            if "rebuild" == harasser:
                method = self.launch_rebuild
                ranks = self.params.get(
                    "ranks_to_kill", "/run/" + harasser + "/*")
                targets = self.params.get("targets", "/run/server_config/*")
                param_list = (ranks, pools, targets)
                name = "REBUILD"
            else:
                raise SoakTestError(
                    "<<FAILED: Harasser {} is not supported. ".format(
                        harasser))
            job = threading.Thread(
                target=method, args=param_list, name=name)
            self.harasser_joblist.append(job)

        # start all harassers
        for job in self.harasser_joblist:
            job.start()

    def harasser_completion(self, timeout):
        """Complete harasser jobs.

        Args:
            timeout (int): timeout in secs

        Returns:
            bool: status

        """
        status = True
        for job in self.harasser_joblist:
            job.join(timeout)
        for job in self.harasser_joblist:
            if job.is_alive():
                self.log.error(
                    "<< HARASSER is alive %s FAILED to join>> ", job.name)
                status &= False
        for harasser, status in self.harasser_results.items():
            if not status:
                self.log.error(
                    "<< HARASSER %s FAILED with Timeout>> ", harasser)
                status &= False
        return status

    def launch_rebuild(self, ranks, pools, targets):
        """Launch the rebuild process.

        Args:
            ranks (list): Server ranks to kill
            pools (list): list of TestPool obj

        """
        self.log.info("<<Launch Rebuild>> at %s", time.ctime())
        status = True
        for pool in pools:
            # Kill the server
            try:
                pool.start_rebuild(ranks, self.d_log)
            except (RuntimeError, TestFail) as error:
                self.log.error("Rebuild failed to start", exc_info=error)
                status &= False
                break
            # Wait for rebuild to start
            try:
                pool.wait_for_rebuild(True)
            except (RuntimeError, TestFail) as error:
                self.log.error(
                    "Rebuild failed waiting to start", exc_info=error)
                status &= False
                break

            # Wait for rebuild to complete
            try:
                pool.wait_for_rebuild(False)
            except (RuntimeError, TestFail) as error:
                self.log.error(
                    "Rebuild failed waiting to finish", exc_info=error)
                status &= False
                break

        with H_LOCK:
            self.harasser_results["REBUILD"] = status

    def create_ior_cmdline(self, job_spec, pool, task_list):
        """Create an IOR cmdline to run in slurm batch.

        Args:

            job_spec (str): ior job in yaml to run
            pool (obj):   TestPool obj

        Returns:
            cmd: cmdline string

        """
        commands = []

        iteration = self.test_iteration
        ior_params = "/run/" + job_spec + "/*"
        # IOR job specs with a list of parameters; update each value
        #   api
        #   transfer_size
        #   block_size

        api_list = self.params.get("api", ior_params + "*")
        tsize_list = self.params.get("transfer_size", ior_params + "*")
        bsize_list = self.params.get("block_size", ior_params + "*")
        oclass_list = self.params.get("daos_oclass", ior_params + "*")
        # check if capable of doing rebuild; if yes then daos_oclass = RP_*GX
        if self.is_harasser("rebuild"):
            oclass_list = self.params.get("daos_oclass", "/run/rebuild/*")
        # update IOR cmdline for each additional IOR obj
        for task in task_list:
            for api in api_list:
                for b_size in bsize_list:
                    for t_size in tsize_list:
                        for o_type in oclass_list:
                            ior_cmd = IorCommand()
                            ior_cmd.namespace = ior_params
                            ior_cmd.get_params(self)
                            if iteration is not None and iteration < 0:
                                ior_cmd.repetitions.update(1000000)
                            if self.job_timeout is not None:
                                ior_cmd.max_duration.update(self.job_timeout)
                            else:
                                ior_cmd.max_duration.update(10)
                            ior_cmd.api.update(api)
                            ior_cmd.block_size.update(b_size)
                            ior_cmd.transfer_size.update(t_size)
                            ior_cmd.daos_oclass.update(o_type)
                            ior_cmd.set_daos_params(self.server_group, pool)

                            # slurm/sbatch cmdline
                            if self.job_manager == "Sbatch":
                                # export the user environment for srun
                                exports = ["ALL"]
                                if ior_cmd.api.value == "MPIIO":
                                    env = {
                                        "CRT_ATTACH_INFO_PATH": os.path.join(
                                            self.basepath, "install/tmp"),
                                        "DAOS_POOL": str(
                                            ior_cmd.daos_pool.value),
                                        "DAOS_CONT": str(
                                            ior_cmd.daos_cont.value),
                                        "MPI_LIB": "\"\"",
                                        "DAOS_SVCL": str(
                                            ior_cmd.daos_svcl.value),
                                        "DAOS_SINGLETON_CLI": 1,
                                        "FI_PSM2_DISCONNECT": 1,
                                        "IOR_HINT__MPI__romio_daos_obj_class":
                                        str(ior_cmd.daos_oclass.value)
                                    }
                                    exports.extend(["{}={}".format(
                                        key, val) for key, val in env.items()])
                                cmd = "srun -l --mpi=pmi2 --export={} {}".format(
                                    ",".join(exports), str(ior_cmd))
                                commands.append([job_spec, cmd, task])
                            else:
                                commands.append([job_spec, ior_cmd, task])
                            self.log.info("<<IOR cmdline>>: %s \n",
                                          commands[-1][1].__str__())
        return commands

    def create_dmg_cmdline(self, job_spec, pool):
        """Create a dmg cmdline to run in slurm batch.

        Args:
            job_params (str): job params from yaml file
            job_spec (str): specific dmg job to run
        Returns:
            cmd: [description]

        """
        cmd = ""
        return cmd

    def build_job_script(self, commands, nodesperjob, job_timeout):
        """Create a slurm batch script that will execute a list of cmdlines.

        Args:
            commands(list): list of list of commandlines and ranks per node
                            commands = [[job1, cmd1, ranks per node], ...]]
            nodesperjob(int): number of nodes to execute job
            job(str): the job type that will be defined in the slurm script
            with /run/"job"/
            tasklist(list): number of tasks to run on each node
            job_timeout(str):  timeout passed to slurm sbatch

        Returns:
            script_list: list of slurm batch scripts

        """
        self.log.info("<<Build Script>> at %s", time.ctime())
        script_list = []
        # Create the sbatch script for each cmdline
        for job_cmd_tasks in commands:
            job = job_cmd_tasks[0]
            cmd = job_cmd_tasks[1]
            tasks = job_cmd_tasks[2]
            output = os.path.join(
                self.rem_pass_dir, "%N_" + self.test_name +
                "_" + job +
                "_results.out_%j_%t_" + str(tasks) + "_")
            num_tasks = self.nodesperjob * tasks
            sbatch = {
                    "ntasks-per-node": tasks,
                    "ntasks": num_tasks,
                    "time": str(job_timeout) + ":00",
                    "partition": self.partition_clients,
                    "exclude": NodeSet.fromlist(self.exclude_slurm_nodes)
                    }
            script = slurm_utils.write_slurm_script(
                self.rem_pass_dir, job, output, self.nodesperjob,
                [cmd], sbatch)
            script_list.append(script)
        return script_list

    def job_setup(self, job, pool, job_manager):
        """Create the cmdline needed to launch job.

        Args:
            job(str): single job from test params list of jobs to run
            pool (obj): TestPool obj
            job_manager (str): job manager user to create job cmdline

        Returns:
            job_cmdlist: list cmdline that can be launched
                         by specifed job manager

        """
        job_cmdlist = []

        self.log.info(
            "<<Job_Setup %s >> at %s", self.test_name, time.ctime())

        # nodesperjob = -1 indicates to use all nodes in client hostlist
        if self.nodesperjob < 0:
            self.nodesperjob = len(self.hostlist_clients)

        if len(self.hostlist_clients)/self.nodesperjob < 1:
            raise SoakTestError(
                "<<FAILED: There are only {} client nodes for this job. "
                "Job requires {}".format(
                    len(self.hostlist_clients), self.nodesperjob))

        #  get the list of cmdlines for the job

        if "ior" in job:
            # commands = [[job, command, #tasks per node], []]
            job_cmds_tasks = self.create_ior_cmdline(
                job, pool, self.task_list)
        elif "dmg" in job:
            # create dmg cmdline
            job_cmds_tasks = self.create_dmg_cmdline(job, pool)
        else:
            raise SoakTestError(
                "<<FAILED: Job {} is not supported. ".format(
                    self.job))

        # Create slurm job
        if self.job_manager == "Sbatch":
            # queue up slurm script and register a callback to retrieve
            # results.  The slurm batch script are single cmdline for now.
            # scripts is a list of slurm batch scripts with a
            # single cmdline
            scripts = self.build_job_script(
                job_cmds_tasks, self.nodesperjob, self.job_timeout)
            job_cmdlist.extend(scripts)
        else:
            raise SoakTestError(
                "<<FAILED: Job manager {} is not supported. ".format(
                    self.job_manager))
        return job_cmdlist

    def job_startup(self, job_cmdlist):
        """Submit job batch script.

        Args:
            job_cmdlist (list): list of jobs to execute
        Returns:
            job_id_list: IDs of each job submitted to slurm.

        """
        self.log.info(
            "<<Job Startup - %s >> at %s", self.test_name, time.ctime())
        job_id_list = []

        # Before the slurm scripts have been submitted clear any allocations
        # from the queue on the client nodes
        status = process.system("scancel -w {}".format(
            NodeSet.fromlist(self.hostlist_clients)))
        if status > 0:
            raise SoakTestError(
                "<<FAILED>>: scancel failed to cancel {}".format(
                    self.hostlist_clients))
        # job_cmdlist is a list of batch scrippt files
        for script in job_cmdlist:
            try:
                job_id = slurm_utils.run_slurm_script(str(script))
            except slurm_utils.SlurmFailed as error:
                self.log.error(error)
                # Force the test to exit with failure
                job_id = None

            if job_id:
                self.log.info(
                    "<<Job %s started with %s >> at %s",
                    job_id, script, time.ctime())
                slurm_utils.register_for_job_results(
                    job_id, self, maxwait=self.test_timeout)
                # keep a list of the job_id's
                job_id_list.append(int(job_id))
            else:
                # one of the jobs failed to queue; exit on first fail for now.
                err_msg = "Slurm failed to submit job for {}".format(script)
                job_id_list = []
                raise SoakTestError(
                    "<<FAILED:  Soak {}: {}>>".format(self.test_name, err_msg))
        return job_id_list

    def job_completion(self, job_id_list):
        """Wait for job completion and cleanup.

        Args:
            job_id_list: IDs of each job submitted to slurm
        """
        self.log.info(
            "<<Job Completion - %s >> at %s", self.test_name, time.ctime())

        # If there is nothing to do; exit
        if len(job_id_list) > 0:
            # wait for all the jobs to finish
            while len(self.soak_results) < len(job_id_list):
                # self.log.info(
                #       "<<Waiting for results %s >>", self.soak_results))
                # allow time for jobs to execute on nodes
                time.sleep(2)
            # check for job COMPLETED and remove it from the job queue
            for job, result in self.soak_results.items():
                # The queue include status of "COMPLETING"
                if result == "COMPLETED":
                    job_id_list.remove(int(job))
                else:
                    self.log.info(
                        "<< Job %s failed with status %s>>", job, result)
            if len(job_id_list) > 0:
                self.log.info(
                    "<<Cancel jobs in queue with id's %s >>", job_id_list)
                for job in job_id_list:
                    status = slurm_utils.cancel_jobs(int(job))
                    if status == 0:
                        self.log.info("<<Job %s successfully cancelled>>", job)
                        # job_id_list.remove(int(job))
                    else:
                        self.log.info("<<Job %s could not be killed>>", job)
            # enable ssh to all client nodes
            # Reallocated nodes to allow "ssh" on all nodes
            status = process.system(
                "salloc -w {} --partition={} --time=05:00".format(
                    NodeSet.fromlist(
                        self.hostlist_clients), self.partition_clients))
            if status > 0:
                raise SoakTestError(
                    "<<FAILED>>: salloc failed on nodes {}".format(
                        self.hostlist_clients))
            # gather all the logfiles for this pass and cleanup test nodes
            # If there is a failure the files can be gathered again in Teardown
            try:
                self.remote_copy(
                    self.node_list, self.rem_pass_dir, self.outputsoakdir)
            except SoakTestError as error:
                self.log.info("Remote copy failed with %s", error)
            self.soak_results = {}
        return job_id_list

    def execute_jobs(self, jobs, pools, job_manager):
        """Execute the overall soak test.

        Args:
            jobs (list):  list of jobs to create cmdlines and execute
            pools (list): list of TestPool obj
            job_manager(str):  job manager used to launch job

        Raise:
            SoakTestError

        """
        cmdlist = []
        # Create the remote log directories from new loop/pass
        self.rem_pass_dir = self.log_dir + "/pass" + str(self.loop)
        self.local_pass_dir = self.outputsoakdir + "/pass" + str(self.loop)
        result = pcmd(
            NodeSet.fromlist(self.hostlist_clients),
            "mkdir -p {}".format(self.rem_pass_dir),
            verbose=False)
        if len(result) > 1 or 0 not in result:
            raise SoakTestError(
                "<<FAILED: logfile directory not"
                "created on clients>>: {}".format(", ".join(
                    [str(result[key]) for key in result if key != 0])))

        # Create local log directory
        os.makedirs(self.local_pass_dir)

        # Setup cmdlines for job with specified pool
        if len(pools) < len(jobs):
            raise SoakTestError(
                "<<FAILED: There are not enough pools to run this test>>")

        for index, job in enumerate(jobs):
            cmdlist.extend(self.job_setup(job, pools[index], job_manager))

        # if Sbatch; cmdlist is a list of batch scripts
        if job_manager == "Sbatch":
            # Gather the job_ids
            self.job_id_list = self.job_startup(cmdlist)

            # Initialize the failed_job_list to job_list so that any
            # unexpected failures will clear the squeue in tearDown
            self.failed_job_id_list = self.job_id_list

            # Wait for jobs to finish and cancel/kill jobs if necessary
            self.failed_job_id_list = self.job_completion(self.job_id_list)

            # launch harassers if defined and enabled
            if self.h_list and self.loop > 1:
                self.log.info("<<Harassers are enabled>>")
                self.launch_harassers(self.h_list, pools)
                if not self.harasser_completion(self.harasser_timeout):
                    pass
                    # Continue on error for now but report the error
                    # raise SoakTestError("<<FAILED: Harassers failed ")
                # rebuild can only run once for now
                if "rebuild" in self.h_list:
                    self.h_list.remove("rebuild")

            # Test fails on first error but could use continue on error here
            if len(self.failed_job_id_list) > 0:
                raise SoakTestError(
                    "<<FAILED: The following jobs failed {} >>".format(
                        " ,".join(
                            str(j_id) for j_id in self.failed_job_id_list)))
        else:
            raise SoakTestError(
                "<<FAILED: Job manager {} is not supported. ".format(
                    self.job_manager))

    def run_soak(self, test_param):
        """Run the soak test specified by the test params.

        Args:
            test_param (str): test_params from yaml file

        """
        pool_list = self.params.get("poollist", test_param + "*")
        self.test_timeout = self.params.get("test_timeout", test_param)
        self.job_timeout = self.params.get("job_timeout", test_param)
        self.job_manager = self.params.get("jobmanager", test_param)
        self.test_name = self.params.get("name", test_param)
        self.job_list = self.params.get("joblist", test_param + "*")
        self.nodesperjob = self.params.get("nodesperjob", test_param)
        self.test_iteration = self.params.get("iteration", test_param)
        self.task_list = self.params.get("taskspernode", test_param + "*")
        self.soak_test_name = test_param.split("/")[2]
        self.h_list = self.params.get("harasserlist", test_param + "*")
        self.harasser_timeout = self.params.get("harasser_timeout", test_param)
        self.job_id_list = []
        self.start_time = time.time()
        self.end_time = self.start_time + self.test_timeout
        self.harasser_joblist = []
        self.soak_results = {}
        self.harasser_results = {}

        rank = self.params.get("rank", "/run/container_reserved/*")
        if self.h_list is not None and "rebuild" in self.h_list:
            obj_class = "_".join(["OC", str(
                self.params.get("daos_oclass", "/run/rebuild/*")[0])])
        else:
            obj_class = self.params.get(
                "object_class", "/run/container_reserved/*")

        # Create the reserved pool with data
        self.pool = []
        self.add_pools(["pool_reserved"])
        self.pool[0].connect()
        self.container = TestContainer(self.pool[0])
        self.container.namespace = "/run/container_reserved"
        self.container.get_params(self)
        self.container.create()
        self.container.write_objects(rank, obj_class)

        # cleanup soak log directories before test on all nodes
        result = pcmd(
            NodeSet.fromlist(self.node_list),
            "rm -rf {}".format(self.log_dir),
            verbose=False)
        if len(result) > 1 or 0 not in result:
            raise SoakTestError(
                "<<FAILED: Soak directories not removed"
                "from clients>>: {}".format(", ".join(
                    [str(result[key]) for key in result if key != 0])))

        self.log.info("<<START %s >> at %s", self.test_name, time.ctime())
        while time.time() < self.end_time:
            # Start new pass
            start_loop_time = time.time()
            self.log.info("<<Soak1 PASS %s: time until done %s>>", self.loop, (
                self.end_time - time.time()))

            # Create all specified pools
            self.add_pools(pool_list)
            self.log.info(
                "Current pools: %s",
                " ".join([pool.uuid for pool in self.pool]))
            try:
                self.execute_jobs(
                    self.job_list, self.pool[1:], self.job_manager)
            except SoakTestError as error:
                self.fail(error)
            errors = self.destroy_pools(self.pool[1:])
            # remove the test pools from self.pool; preserving reserved pool
            self.pool = [self.pool[0]]
            self.log.info(
                "Current pools: %s",
                " ".join([pool.uuid for pool in self.pool]))
            self.assertEqual(len(errors), 0, "\n".join(errors))
            # Break out of loop if smoke
            if "smoke" in self.test_name:
                break
            loop_time = time.time() - start_loop_time
            self.log.info(
                "<<PASS %s completed in %s seconds>>", self.loop, loop_time)
            # if the time left if less than a loop exit now
            if self.end_time - time.time() < loop_time:
                break
            self.loop += 1
        # Check that the reserve pool is still allocated
        self.assertTrue(
                self.pool[0].check_files(self.hostlist_servers),
                "Pool data not detected on servers")
        # Verify the data after soak is done
        self.assertTrue(
                self.container.read_objects(),
                "Data verification error on reserved pool"
                "after SOAK completed")

    def setUp(self):
        """Define test setup to be done."""
        self.log.info("<<setUp Started>> at %s", time.ctime())
        # Start the servers in the test case
        self.setup_start_servers = True
        super(Soak, self).setUp()

        self.failed_job_id_list = []
        self.exclude_slurm_nodes = []
        # Initialize loop param for all tests
        self.loop = 1

        # Setup logging directories for soak logfiles
        # self.output dir is an avocado directory .../data/
        self.log_dir = self.params.get("logdir", "/run/*")
        self.outputsoakdir = self.outputdir + "/soak"

        # Create the remote log directories on all client nodes
        self.rem_pass_dir = self.log_dir + "/pass" + str(self.loop)
        self.local_pass_dir = self.outputsoakdir + "/pass" + str(self.loop)

        # Fail if slurm partition daos_client is not defined
        if not self.partition_clients:
            raise SoakTestError(
                "<<FAILED: Partition is not correctly setup for daos "
                "slurm partition>>")

        # Check if the server nodes are in the client list;
        # this will happen when only one partition is specified
        for host_server in self.hostlist_servers:
            if host_server in self.hostlist_clients:
                self.hostlist_clients.remove(host_server)
                self.exclude_slurm_nodes.append(host_server)
        self.log.info(
                "<<Updated hostlist_clients %s >>", self.hostlist_clients)
        # include test node for log cleanup; remove from client list
        test_node = [socket.gethostname().split('.', 1)[0]]
        if test_node[0] in self.hostlist_clients:
            self.hostlist_clients.remove(test_node[0])
            self.exclude_slurm_nodes.append(test_node[0])
            self.log.info(
                "<<Updated hostlist_clients %s >>", self.hostlist_clients)
        if len(self.hostlist_clients) < 1:
            self.fail("There are no nodes that are client only;"
                      "check if the partition also contains server nodes")
        self.node_list = self.hostlist_clients + test_node

    def tearDown(self):
        """Define tearDown and clear any left over jobs in squeue."""
        self.log.info("<<tearDown Started>> at %s", time.ctime())
        # clear out any jobs in squeue;
        errors_detected = False
        if len(self.failed_job_id_list) > 0:
            self.log.info(
                "<<Cancel jobs in queue with ids %s >>",
                self.failed_job_id_list)
            status = process.system("scancel --partition {}".format(
                self.partition_clients))
            if status > 0:
                errors_detected = True
        # One last attempt to copy any logfiles from client nodes
        try:
            self.remote_copy(
                self.node_list, self.rem_pass_dir, self.outputsoakdir)
        except SoakTestError as error:
            self.log.info("Remote copy failed with %s", error)
            errors_detected = True
        super(Soak, self).tearDown()
        if errors_detected:
            self.fail("Errors detected cancelling slurm jobs in tearDown()")

    def test_soak_smoke(self):
        """Run soak smoke.

        Test ID: DAOS-2192
        Test Description: This will create a slurm batch job that runs IOR
        with DAOS with the number of processes determined by the number of
        nodes.
        For this test a single pool will be created.  It will run for ~10 min
        :avocado: tags=soak_smoke
        """
        test_param = "/run/smoke/"
        self.run_soak(test_param)

    def test_soak_stress(self):
        """Run all soak tests .

        Test ID: DAOS-2256
        Test Description: This will create a slurm batch job that runs
        various jobs defined in the soak yaml
        This test will run for the time specififed in
        /run/test_timeout.

        :avocado: tags=soak,soak_stress
        """
        test_param = "/run/soak_stress/"
        self.run_soak(test_param)

    def test_soak_harassers(self):
        """Run all soak tests with harassers.

        Test ID: DAOS-2511
        Test Description: This will create a soak job that runs
        various harassers  defined in the soak yaml
        This test will run for the time specififed in
        /run/test_timeout.

        :avocado: tags=soak,soak_harassers
        """
        test_param = "/run/soak_harassers/"
        self.run_soak(test_param)


def main():
    """Kicks off test with main function."""


if __name__ == "__main__":
    main()
