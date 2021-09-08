#!/usr/bin/python
"""
(C) Copyright 2019-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import os
import random
import time
import threading
import re

from general_utils import run_command, DaosTestError
from ClusterShell.NodeSet import NodeSet

W_LOCK = threading.Lock()


class SlurmFailed(Exception):
    """Thrown when something goes wrong with slurm."""


def cancel_jobs(job_id):
    """Cancel slurms jobs.

    Args:
        job_id (int): slurm job id

    Returns:
        int: return status from scancel command

    """
    result = run_command("scancel {}".format(job_id), raise_exception=False)
    if result.exit_status > 0:
        raise SlurmFailed(
            "Slurm: scancel failed to kill job {}".format(job_id))
    return result.exit_status


def create_slurm_partition(nodelist, name):
    """Create a slurm partition for soak jobs.

    Client nodes will be allocated for this partition.

    Args:
        nodelist (list): list of nodes for job allocation
        name (str): partition name

    Returns:
        int: return status from scontrol command

    """
    # If the partition exists; delete it because if may have wrong nodes
    command = "scontrol delete PartitionName={}".format(name)
    result = run_command(command, raise_exception=False)
    if result.exit_status == 0:
        command = "scontrol create PartitionName={} Nodes={}".format(
            name, ",".join(nodelist))
        result = run_command(command, raise_exception=False)
    return result.exit_status


def delete_slurm_partition(name):
    """Remove the partition from slurm.

    Args:
        name (str): partition name

    Returns:
        int: return status from scontrol command

    """
    # If the partition exists; delete it because if may have wrong nodes
    command = "scontrol delete PartitionName={}".format(name)
    result = run_command(command, raise_exception=False)
    return result.exit_status


def get_reserved_nodes(reservation, partition):
    """Get the reserved nodes.

    Args:
        reservation (str): reservation name
        partition (str): partition name

    Returns:
        list: return list of reserved nodes in partition

    """
    partition_hosts = []
    hosts = []
    # Get the partition information
    cmd = "scontrol show partition {}".format(partition)
    partition_result = run_command(cmd, raise_exception=False)

    if partition_result:
        # Get the list of hosts from the reservation information
        output = partition_result.stdout_text
        match = re.search(r"\sNodes=(\S+)", str(output))
        if match is not None:
            partition_hosts = list(NodeSet(match.group(1)))
            print("partition_hosts = {}".format(partition_hosts))
            # partition hosts exists; continue with valid partition
            cmd = "scontrol show reservation {}".format(reservation)
            reservation_result = run_command(cmd, raise_exception=False)
            if reservation_result:
                # Get the list of hosts from the reservation information
                output = reservation_result.stdout_text
                match = re.search(r"\sNodes=(\S+)", str(output))
                if match is not None:
                    reservation_hosts = list(NodeSet(match.group(1)))
                    print("reservation_hosts = {}".format(reservation_hosts))
                    if set(reservation_hosts).issubset(set(partition_hosts)):
                        hosts = reservation_hosts
    return hosts


def write_slurm_script(path, name, output, nodecount, cmds, uniq, sbatch=None):
    """Generate a script for submitting a job to slurm.

    path      --where to write the script file
    name      --job name
    output    --where to put the output (full path)
    nodecount --number of compute nodes to execute on
    cmds      --shell commands that are to be executed
    sbatch    --dictionary containing other less often used parameters to
                sbatch, e.g. mem:100
    uniq string  --a unique string to append to the job and log files
    returns   --the full path of the script
    """
    if name is None or nodecount is None or cmds is None:
        raise SlurmFailed("Bad parameters passed for slurm script.")
    if uniq is None:
        uniq = random.randint(1, 100000)

    if not os.path.exists(path):
        os.makedirs(path)
    scriptfile = path + '/jobscript' + "_" + str(uniq) + ".sh"
    with open(scriptfile, 'w') as script_file:
        # identify what be used to run this script
        script_file.write("#!/bin/bash\n#\n")

        # write the mandatory parameters
        script_file.write("#SBATCH --job-name={}\n".format(name))
        script_file.write("#SBATCH --nodes={}\n".format(nodecount))
        script_file.write("#SBATCH --distribution=cyclic\n")
        if output is not None:
            output = output + str(uniq)
            script_file.write("#SBATCH --output={}\n".format(output))
        if sbatch:
            for key, value in list(sbatch.items()):
                if key == "error":
                    value = value + str(uniq)
                script_file.write("#SBATCH --{}={}\n".format(key, value))
        script_file.write("\n")

        # debug
        script_file.write("echo \"nodes: \" $SLURM_JOB_NODELIST \n")
        script_file.write("echo \"node count: \" $SLURM_JOB_NUM_NODES \n")
        script_file.write("echo \"job name: \" $SLURM_JOB_NAME \n")

        for cmd in list(cmds):
            script_file.write(cmd + "\n")
    return scriptfile


def run_slurm_script(script, logfile=None):
    """Run slurm script.

    Args:
        script (str): script file suitable to by run by slurm
        logfile (str, optional): logfile to generate. Defaults to None.

    Raises:
        SlurmFailed: if there is an error obtaining the slurm job id

    Returns:
        str: the job ID, which is used as a handle for other functions

    """
    job_id = None
    if logfile is not None:
        script = " -o " + logfile + " " + script
    cmd = "sbatch " + script
    try:
        result = run_command(cmd, timeout=10)
    except DaosTestError as error:
        raise SlurmFailed("job failed : {}".format(error)) from error
    if result:
        output = result.stdout_text
        match = re.search(r"Submitted\s+batch\s+job\s+(\d+)", str(output))
        if match is not None:
            job_id = match.group(1)
    return job_id


def check_slurm_job(handle):
    """Get the state of a job initiated via slurm.

    Args:
        handle (str): slurm job id

    Returns:
        str: one of the slurm defined JOB_STATE_CODES strings plus one extra
            UNKNOWN if the handle doesn't match a known slurm job.

    """
    command = "scontrol show job {}".format(handle)
    result = run_command(command, raise_exception=False, verbose=False)
    match = re.search(r"JobState=([a-zA-Z]+)", result.stdout_text)
    if match is not None:
        state = match.group(1)
    else:
        state = "UNKNOWN"
    return state


def register_for_job_results(handle, test, maxwait=3600):
    """
    Register a callback for a slurm soak test job.

    handle   --slurm job id
    test     --object with a job_done callback function
    maxwait  --maximum time to wait in seconds, defaults to 1 hour
    returns  --None

    """
    params = {"handle": handle, "maxwait": maxwait, "test_obj": test}
    athread = threading.Thread(target=watch_job,
                               kwargs=params)
    athread.start()


def watch_job(handle, maxwait, test_obj):
    """Watch for a slurm job to finish use callback function with the result.

    handle   --the slurm job handle
    maxwait  --max time in seconds to wait
    test_obj --whom to notify when its done

    return   --none, all results handled through callback
    """
    wait_time = 0
    while True:
        state = check_slurm_job(handle)
        if state in ("PENDING", "RUNNING", "COMPLETING", "CONFIGURING"):
            if wait_time > maxwait:
                state = "MAXWAITREACHED"
                print("Job {} has timedout after {} secs".format(handle,
                                                                 maxwait))
                break
            wait_time += 5
            time.sleep(5)
        else:
            break

    print("FINAL STATE: slurm job {} completed with : {} at {}\n".format(
        handle, state, time.ctime()))
    params = {"handle": handle, "state": state}
    with W_LOCK:
        test_obj.job_done(params)


def srun_str(hosts, cmd, srun_params=None):
    """Create string of cmd with srun and params.

    Args:
        hosts (str): hosts to allocate
        cmd (str): cmdline to execute
        srun_params(dict): additional params for srun

    Returns:
        Cmd: str of cmdline wrapped in srun with params

    """
    params_list = []
    params = ""
    if hosts is not None:
        params_list.append("--nodelist {}".format(hosts))
    if srun_params is not None:
        for key, value in list(srun_params.items()):
            params_list.extend(["--{}={}".format(key, value)])
            params = " ".join(params_list)
    cmd = "srun {} {}".format(params, cmd)
    return str(cmd)


def srun(hosts, cmd, srun_params=None, timeout=30):
    """Run srun cmd on slurm partition.

    Args:
        hosts (str): hosts to allocate
        cmd (str): cmdline to execute
        srun_params(dict): additional params for srun

    Returns:
        CmdResult: object containing the result (exit status, stdout, etc.) of
            the srun command

    """
    cmd = srun_str(hosts, cmd, srun_params)
    try:
        result = run_command(cmd, timeout)
    except DaosTestError as error:
        result = None
        raise SlurmFailed("srun failed : {}".format(error)) from error
    return result
