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
from agent_utils import (DaosAgentTransportCredentials, DaosAgentYamlParameters,
                         DaosAgentCommand, DaosAgentManager)
from command_daos_utils import CommonConfig
from ior_utils import IorCommand
from fio_utils import FioCommand
from dfuse_utils import Dfuse
from job_manager_utils import Srun
import slurm_utils
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from ClusterShell.NodeSet import NodeSet
import socket
from avocado.utils import process


class SoakTestError(Exception):
    """Soak exception class."""


class Soak(TestWithServers):
    """Execute DAOS Soak test cases.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a SoakBase object."""
        super(Soak, self).__init__(*args, **kwargs)
        self.failed_job_id_list = None
        self.test_log_dir = None
        self.exclude_slurm_nodes = None
        self.loop = None
        self.log_dir = None
        self.outputsoakdir = None
        self.test_name = None
        self.local_pass_dir = None
        self.dfuse = None
        self.test_timeout = None
        self.job_timeout = None
        self.nodesperjob = None
        self.task_list = None
        self.soak_results = None
        self.srun_params = None
        self.pool = None
        self.container = None
        self.test_iteration = None

    def job_done(self, args):
        """Call this function when a job is done.

        Args:
            args (list):handle --which job, i.e. the job ID,
                        state  --string indicating job completion status
        """
        self.soak_results[args["handle"]] = args["state"]

    def add_pools(self, pool_names):
        """Create a list of pools that the various tests use for storage.

        Args:
            pool_names: list of pool namespaces from yaml file
                        /run/<test_params>/poollist/*
        """
        for pool_name in pool_names:
            path = "".join(["/run/", pool_name, "/*"])
            # Create a pool and add it to the overall list of pools
            self.pool.append(
                TestPool(
                    self.context, self.log, dmg=self.server_managers[0].dmg))
            self.pool[-1].namespace = path
            self.pool[-1].get_params(self)
            self.pool[-1].create()
            self.log.info("Valid Pool UUID is %s", self.pool[-1].uuid)

    def get_remote_logs(self):
        """Copy files from remote dir to local dir.

        Raises:
            SoakTestError: if there is an error with the remote copy

        """
        # copy the files from the remote
        # TO-DO: change scp
        this_host = socket.gethostname()
        result = slurm_utils.srun(
            NodeSet.fromlist(self.hostlist_clients),
            "bash -c \"scp -p -r {0} {1}:{0}/.. && rm -rf {0}/*\"".format(
                self.test_log_dir, this_host),
            self.srun_params)
        if result.exit_status == 0:
            cmd = "cp -R {0}/ \'{1}\'; rm -rf {0}/*".format(
                self.test_log_dir, self.outputsoakdir)
            try:
                result = process.run(cmd, shell=True, timeout=30)
            except process.CmdError as error:
                raise SoakTestError(
                    "<<FAILED: Soak remote logfiles not copied"
                    "to avocado data dir {} - check /tmp/soak "
                    "on nodes {}>>".format(error, self.hostlist_clients))
        else:
            raise SoakTestError(
                "<<FAILED: Soak remote logfiles not copied "
                "from clients>>: {}".format(self.hostlist_clients))

    def create_ior_cmdline(self, job_spec, pool, ppn):
        """Create an IOR cmdline to run in slurm batch.

        Args:

            job_spec (str): ior job in yaml to run
            pool (obj):   TestPool obj
            ppn(int): number of tasks to run on each node

        Returns:
            cmd: cmdline string

        """
        commands = []

        iteration = self.test_iteration
        ior_params = "/run/" + job_spec + "/*"
        # IOR job specs with a list of parameters; update each value
        api_list = self.params.get("api", ior_params + "*")
        tsize_list = self.params.get("transfer_size", ior_params + "*")
        bsize_list = self.params.get("block_size", ior_params + "*")
        oclass_list = self.params.get("daos_oclass", ior_params + "*")
        # update IOR cmdline for each additional IOR obj
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
                        # srun cmdline
                        nprocs = self.nodesperjob * ppn
                        env = ior_cmd.get_default_env("srun", self.tmp)
                        if ior_cmd.api.value == "MPIIO":
                            env["DAOS_CONT"] = ior_cmd.daos_cont.value
                        cmd = Srun(ior_cmd)
                        cmd.assign_environment(env, True)
                        cmd.assign_processes(nprocs)
                        cmd.ntasks_per_node.update(ppn)
                        commands.append(cmd.__str__())

                        self.log.info(
                            "<<IOR cmdline>>: %s \n", commands[-1].__str__())
        return commands

    def create_dfuse_cont(self, pool):
        """Create a TestContainer object to be used to create container.

        Args:

            pool (obj):   TestPool obj

        Returns:
            cuuid: container uuid

        """
        # TO-DO: use daos tool when available
        # This method assumes that doas agent is running on test node

        cmd = "daos cont create --pool={} --svc={} --type=POSIX".format(
            pool.uuid, ":".join(
                [str(item) for item in pool.svc_ranks]))
        try:
            result = process.run(cmd, shell=True, timeout=30)
        except process.CmdError as error:
            raise SoakTestError(
                "<<FAILED: Dfuse container failed {}>>".format(error))
        self.log.info("Dfuse Container UUID = %s", result.stdout.split()[3])
        return result.stdout.split()[3]

    def start_dfuse(self, pool):
        """Create a DfuseCommand object to start dfuse.

        Args:

            pool (obj):   TestPool obj
        """
        # Get Dfuse params
        self.dfuse = Dfuse(self.hostlist_clients, self.tmp, self.basepath)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(pool)
        self.dfuse.set_dfuse_cont_param(self.create_dfuse_cont(pool))

        # create dfuse mount point
        cmd = "mkdir -p {}".format(self.dfuse.mount_dir.value)
        params = self.srun_params
        params["export"] = "all"
        params["ntasks-per-node"] = 1
        result = slurm_utils.srun(
            NodeSet.fromlist(self.hostlist_clients), cmd, params)
        if result.exit_status > 0:
            raise SoakTestError(
                "<<FAILED: Dfuse mountpoint {} not created>>".format(
                    self.dfuse.mount_dir.value))

        cmd = self.dfuse.__str__()
        result = slurm_utils.srun(
            NodeSet.fromlist(self.hostlist_clients), cmd, params)
        if result.exit_status > 0:
            raise SoakTestError(
                "<<FAILED: Dfuse failed to start>>")

    def create_fio_cmdline(self, job_spec, pool):
        """Create the FOI commandline.

        Args:

            job_spec (str): fio job in yaml to run
            pool (obj):   TestPool obj
            ppn(int): number of tasks to run on each node

        Returns:
            cmd(list): list of cmdlines

        """
        commands = []

        fio_namespace = "/run/{}".format(job_spec)
        # test params
        bs_list = self.params.get("blocksize", fio_namespace + "/soak/*")
        size_list = self.params.get("size", fio_namespace + "/soak/*")
        rw_list = self.params.get("rw", fio_namespace + "/soak/*")
        # Get the parameters for Fio
        fio_cmd = FioCommand()
        fio_cmd.namespace = "{}/*".format(fio_namespace)
        fio_cmd.get_params(self)
        for blocksize in bs_list:
            for size in size_list:
                for rw in rw_list:
                    # update fio params
                    fio_cmd.update(
                        "global", "blocksize", blocksize,
                        "fio --name=global --blocksize")
                    fio_cmd.update(
                        "global", "size", size,
                        "fio --name=global --size")
                    fio_cmd.update(
                        "global", "rw", rw,
                        "fio --name=global --rw")
                    # start dfuse if api is POSIX
                    if fio_cmd.api.value == "POSIX":
                        # Connect to the pool, create container
                        # and then start dfuse
                        self.start_dfuse(pool)
                        fio_cmd.update(
                            "global", "directory",
                            self.dfuse.mount_dir.value,
                            "fio --name=global --directory")
                    # fio command
                    commands.append(fio_cmd.__str__())
                    self.log.info(
                        "<<FIO cmdline>>: %s \n", commands[-1])
        return commands

    def build_job_script(self, commands, job, ppn, nodesperjob):
        """Create a slurm batch script that will execute a list of cmdlines.

        Args:
            commands(list): commandlines
            job(str): the job name that will be defined in the slurm script
            ppn(int): number of tasks to run on each node

        Returns:
            script_list: list of slurm batch scripts

        """
        self.log.info("<<Build Script>> at %s", time.ctime())
        script_list = []

        # Start the daos_agent in the batch script for now
        agent_launch_cmds = [
            "mkdir -p {}".format(os.environ.get("DAOS_TEST_LOG_DIR"))]
        agent_launch_cmds.append(self.get_agent_launch_command())

        # Create the sbatch script for each cmdline
        for cmd in commands:
            output = os.path.join(self.test_log_dir, "%N_" +
                                  self.test_name + "_" + job + "_%j_%t_" +
                                  str(ppn) + "_")
            sbatch = {
                "time": str(self.job_timeout) + ":00",
                "exclude": NodeSet.fromlist(self.exclude_slurm_nodes)
                }
            # include the cluster specific params
            sbatch.update(self.srun_params)
            script = slurm_utils.write_slurm_script(
                self.test_log_dir, job, output, nodesperjob,
                agent_launch_cmds + [cmd], sbatch)
            script_list.append(script)
        return script_list

    def get_agent_launch_command(self):
        """Get the command to launch the daos_agent command.

        Returns:
            str: the command to launch the daos_agent command as a background
                process

        """
        # Create the common config yaml entries for the daos_agent command
        transport = DaosAgentTransportCredentials()
        config_file = self.get_config_file(self.server_group, "agent")
        common_cfg = CommonConfig(self.server_group, transport)

        # Create an AgentCommand to manage with a new AgentManager object
        agent_cfg = DaosAgentYamlParameters(config_file, common_cfg)
        agent_cmd = DaosAgentCommand(self.bin, agent_cfg)
        agent_mgr = DaosAgentManager(agent_cmd, "Srun")
        agent_mgr.manager.ntasks_per_node.value = 1

        # Get any daos_agent command/yaml options from the test yaml
        agent_mgr.manager.job.get_params(self)

        # Assign the access points list
        agent_mgr.set_config_value("access_points", self.hostlist_servers[:1])

        # TO-DO:  daos_agents start with systemd
        return " ".join([str(agent_mgr), "&"])

    def job_setup(self, job, pool):
        """Create the cmdline needed to launch job.

        Args:
            job(str): single job from test params list of jobs to run
            pool (obj): TestPool obj

        Returns:
            job_cmdlist: list cmdline that can be launched
                         by specifed job manager

        """
        job_cmdlist = []
        commands = []
        scripts = []
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

        if "ior" in job:
            for ppn in self.task_list:
                commands = self.create_ior_cmdline(job, pool, ppn)
                # scripts are single cmdline
                scripts = self.build_job_script(
                    commands, job, ppn, self.nodesperjob)
                job_cmdlist.extend(scripts)
        elif "fio" in job:
            commands = self.create_fio_cmdline(job, pool)
            # scripts are single cmdline
            scripts = self.build_job_script(commands, job, 1, 1)
            job_cmdlist.extend(scripts)
        else:
            raise SoakTestError(
                "<<FAILED: Job {} is not supported. ".format(
                    self.job))
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
        Returns:
            failed_job_id_list: IDs of each job that failed in slurm

        """
        self.log.info(
            "<<Job Completion - %s >> at %s", self.test_name, time.ctime())

        # If there is nothing to do; exit
        if job_id_list:
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
            if job_id_list:
                self.log.info(
                    "<<Cancel jobs in queue with id's %s >>", job_id_list)
                for job in job_id_list:
                    status = slurm_utils.cancel_jobs(int(job))
                    if status == 0:
                        self.log.info("<<Job %s successfully cancelled>>", job)
                    else:
                        self.log.info("<<Job %s could not be killed>>", job)
            # gather all the logfiles for this pass and cleanup test nodes
            # If there is a failure the files can be gathered again in Teardown
            try:
                self.get_remote_logs()
            except SoakTestError as error:
                self.log.info("Remote copy failed with %s", error)
            self.soak_results = {}
        return job_id_list

    def execute_jobs(self, jobs, pools):
        """Execute the overall soak test.

        Args:
            pools (list): list of TestPool obj - self.pool[1:]

        Raise:
            SoakTestError

        """
        cmdlist = []
        # Create the remote log directories from new loop/pass
        self.test_log_dir = self.log_dir + "/pass" + str(self.loop)
        self.local_pass_dir = self.outputsoakdir + "/pass" + str(self.loop)
        result = slurm_utils.srun(
            NodeSet.fromlist(self.hostlist_clients), "mkdir -p {}".format(
                self.test_log_dir), self.srun_params)
        if result.exit_status > 0:
            raise SoakTestError(
                "<<FAILED: logfile directory not"
                "created on clients>>: {}".format(self.hostlist_clients))

        # Create local log directory
        os.makedirs(self.local_pass_dir)

        # Setup cmdlines for job with specified pool
        if len(pools) < len(jobs):
            raise SoakTestError(
                "<<FAILED: There are not enough pools to run this test>>")

        for index, job in enumerate(jobs):
            cmdlist.extend(self.job_setup(job, pools[index]))

        # Gather the job_ids
        job_id_list = self.job_startup(cmdlist)

        # Initialize the failed_job_list to job_list so that any
        # unexpected failures will clear the squeue in tearDown
        self.failed_job_id_list = job_id_list

        # Wait for jobs to finish and cancel/kill jobs if necessary
        self.failed_job_id_list = self.job_completion(job_id_list)

        # Test fails on first error but could use continue on error here
        if self.failed_job_id_list:
            raise SoakTestError(
                "<<FAILED: The following jobs failed {} >>".format(
                    " ,".join(
                        str(j_id) for j_id in self.failed_job_id_list)))

    def run_soak(self, test_param):
        """Run the soak test specified by the test params.

        Args:
            test_param (str): test_params from yaml file

        """
        self.soak_results = {}
        self.pool = []
        self.test_timeout = self.params.get("test_timeout", test_param)
        self.job_timeout = self.params.get("job_timeout", test_param)
        self.test_name = self.params.get("name", test_param)
        self.nodesperjob = self.params.get("nodesperjob", test_param)
        self.test_iteration = self.params.get("iteration", test_param)
        self.task_list = self.params.get("taskspernode", test_param + "*")

        job_list = self.params.get("joblist", test_param + "*")
        pool_list = self.params.get("poollist", test_param + "*")
        rank = self.params.get("rank", "/run/container_reserved/*")
        obj_class = self.params.get(
            "object_class", "/run/container_reserved/*")
        slurm_reservation = self.params.get(
            "reservation", "/run/srun_params/*")

        # Srun params
        if self.client_partition is not None:
            self.srun_params = {"partition": self.client_partition}
        if slurm_reservation is not None:
            self.srun_params["reservation"] = slurm_reservation
        # Initialize time
        start_time = time.time()
        end_time = start_time + self.test_timeout
        # Create the reserved pool with data
        # self.pool is a list of all the pools used in soak
        # self.pool[0] will always be the reserved pool
        self.add_pools(["pool_reserved"])

        # TO-DO: Remove when no longer using API calls
        # Start and agent on this host to enable API calls
        agent_groups = {
            self.server_group: [socket.gethostname().split('.', 1)[0]]}
        self.start_agents(agent_groups, self.hostlist_servers)
        self.pool[0].connect()

        # Create the container and populate with a known data
        # TO-DO: use IOR to write and later read verify the data
        self.container = TestContainer(self.pool[0])
        self.container.namespace = "/run/container_reserved/*"
        self.container.get_params(self)
        self.container.create()
        self.container.write_objects(rank, obj_class)

        # cleanup soak log directories before test on all nodes
        result = slurm_utils.srun(
            NodeSet.fromlist(self.hostlist_clients), "rm -rf {}".format(
                self.log_dir), self.srun_params)
        if result.exit_status > 0:
            raise SoakTestError(
                "<<FAILED: Soak directories not removed"
                "from clients>>: {}".format(self.hostlist_clients))
        # cleanup test_node /tmp/soak
        cmd = "rm -rf {}".format(self.log_dir)
        try:
            result = process.run(cmd, shell=True, timeout=30)
        except process.CmdError as error:
            raise SoakTestError(
                "<<FAILED: Soak directory on testnode not removed {}>>".format(
                    error))

        self.log.info("<<START %s >> at %s", self.test_name, time.ctime())
        while time.time() < end_time:
            # Start new pass
            start_loop_time = time.time()
            self.log.info("<<Soak1 PASS %s: time until done %s>>", self.loop, (
                end_time - time.time()))

            # Create all specified pools
            self.add_pools(pool_list)
            self.log.info(
                "Current pools: %s",
                " ".join([pool.uuid for pool in self.pool]))
            try:
                self.execute_jobs(job_list, self.pool[1:])
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
            if end_time - time.time() < loop_time:
                break
            self.loop += 1
        # TO-DO: use IOR
        self.assertTrue(
            self.container.read_objects(),
            "Data verification error on reserved pool after SOAK completed")

    def setUp(self):
        """Define test setup to be done."""
        self.log.info("<<setUp Started>> at %s", time.ctime())
        # Start the daos_agents in the job scripts
        self.setup_start_servers = True
        self.setup_start_agents = False
        super(Soak, self).setUp()

        # Initialize loop param for all tests
        self.loop = 1
        self.exclude_slurm_nodes = []
        # Setup logging directories for soak logfiles
        # self.output dir is an avocado directory .../data/
        self.log_dir = self.params.get("logdir", "/run/*")
        self.outputsoakdir = self.outputdir + "/soak"

        # Create the remote log directories on all client nodes
        self.test_log_dir = self.log_dir + "/pass" + str(self.loop)
        self.local_pass_dir = self.outputsoakdir + "/pass" + str(self.loop)

        # Fail if slurm partition daos_client is not defined
        if not self.client_partition:
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
        if not self.hostlist_clients:
            self.fail("There are no nodes that are client only;"
                      "check if the partition also contains server nodes")
        self.node_list = self.hostlist_clients + test_node

    def tearDown(self):
        """Define tearDown and clear any left over jobs in squeue."""
        self.log.info("<<tearDown Started>> at %s", time.ctime())
        # clear out any jobs in squeue;
        errors_detected = False
        if self.failed_job_id_list:
            self.log.info(
                "<<Cancel jobs in queue with ids %s >>",
                self.failed_job_id_list)
            status = process.system("scancel --partition {}".format(
                self.client_partition))
            if status > 0:
                errors_detected = True
        # One last attempt to copy any logfiles from client nodes
        try:
            self.get_remote_logs()
        except SoakTestError as error:
            self.log.info("Remote copy failed with %s", error)
            errors_detected = True
        if not self.setup_start_agents:
            self.hostlist_clients = [socket.gethostname().split('.', 1)[0]]
        super(Soak, self).tearDown()
        if errors_detected:
            self.fail("Errors detected cancelling slurm jobs in tearDown()")

    def test_soak_smoke(self):
        """Run soak smoke.

        Test ID: DAOS-2192

        Test Description:  This will create a slurm batch job that runs
        various jobs defined in the soak yaml.  It will run for ~10 min

        :avocado: tags=soak_smoke
        """
        test_param = "/run/smoke/"
        self.run_soak(test_param)

    def test_soak_stress(self):
        """Run all soak tests .

        Test ID: DAOS-2256
        Test ID: DAOS-2509
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
