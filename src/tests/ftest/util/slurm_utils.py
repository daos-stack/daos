#!/usr/bin/python
"""
(C) Copyright 2019-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import os
import random
import time
import threading
import re

from ClusterShell.NodeSet import NodeSet, NodeSetParseError

from general_utils import run_command, DaosTestError
from run_utils import run_remote

PACKAGES = ['slurm', 'slurm-example-configs', 'slurm-slurmctld', 'slurm-slurmd']
W_LOCK = threading.Lock()


class SlurmFailed(Exception):
    """Thrown when something goes wrong with slurm."""


def cancel_jobs(job_id):
    """Cancel slurm jobs.

    Args:
        job_id (int): slurm job id

    Raises:
        SlurmFailed: if there is an error cancelling the slurm jobs

    Returns:
        int: return status from scancel command

    """
    result = run_command("scancel {}".format(job_id), raise_exception=False)
    if result.exit_status > 0:
        raise SlurmFailed("Slurm: scancel failed to kill job {}".format(job_id))
    return result.exit_status


def create_partition(log, control, name, hosts, default='yes', max_time='UNLIMITED', state='up'):
    """Create a slurm partition for soak jobs.

    Client nodes will be allocated for this partition.

    Args:
        log (logger): logger for the messages produced by this method
        control (NodeSet): slurm control host
        name (str): slurm partition name
        hosts (NodeSet): hosts to include in the partition
        default (str, optional): _description_. Defaults to 'yes'.
        max_time (str, optional): _description_. Defaults to 'UNLIMITED'.
        state (str, optional): _description_. Defaults to 'up'.

    Returns:
        RemoteCommandResult: results from the scontrol command

    """
    command = ['scontrol', 'create']
    command.append('='.join(['PartitionName', str(name)]))
    command.append('='.join(['Nodes', str(hosts)]))
    command.append('='.join(['Default', str(default)]))
    command.append('='.join(['MaxTime', str(max_time)]))
    command.append('='.join(['State', str(state)]))
    return run_remote(log, control, ' '.join(command))


def delete_partition(log, control, name):
    """Remove the partition from slurm.

    Args:
        log (logger): logger for the messages produced by this method
        control (NodeSet): slurm control host
        name (str): slurm partition name

    Returns:
        int: return status from scontrol command

    """
    command = ['scontrol', 'delete']
    command.append('='.join(['PartitionName', str(name)]))
    return run_remote(log, control, ' '.join(command))


def show_partition(log, control, name):
    """Show the slurm partition.

    Args:
        log (logger): logger for the messages produced by this method
        control (NodeSet): slurm control host
        name (str): slurm partition name

    Returns:
        RemoteCommandResult: results from the scontrol command

    """
    command = ['scontrol', 'show', 'partition', name]
    return run_remote(log, control, ' '.join(command))


def show_reservation(log, control, name):
    """Show the slurm reservation.

    Args:
        log (logger): logger for the messages produced by this method
        control (NodeSet): slurm control host
        name (str): slurm reservation name

    Returns:
        RemoteCommandResult: results from the scontrol command

    """
    command = ['scontrol', 'show', 'reservation', name]
    return run_remote(log, control, ' '.join(command))


def sinfo(log, control):
    """Run the sinfo command.

    Args:
        log (logger): logger for the messages produced by this method
        control (NodeSet): slurm control host

    Returns:
        RemoteCommandResult: results from the sinfo command

    """
    return run_remote(log, control, 'sinfo')


def get_partition_hosts(log, control, partition):
    """Get the hosts defined in the specified slurm partition.

    Args:
        log (logger): logger for the messages produced by this method
        control_host (NodeSet): slurm control host
        partition (str): name of the slurm partition from which to obtain the names

    Raises:
        SlurmFailed: if there is a problem obtaining the hosts from the slurm partition

    Returns:
        NodeSet: slurm partition hosts

    """
    if partition is None:
        return NodeSet()

    # Get the partition name information
    result = show_partition(log, control, partition)
    if not result.passed:
        raise SlurmFailed(f'Unable to obtain hosts from the {partition} slurm partition')

    # Get the list of hosts from the partition information
    try:
        output = '\n'.join(result.all_stdout.values())
        return NodeSet(','.join(re.findall(r'\s+Nodes=(.*)', output)))
    except (NodeSetParseError, TypeError) as error:
        raise SlurmFailed(
            f'Unable to obtain hosts from the {partition} slurm partition output') from error


def get_reservation_hosts(log, control, reservation):
    """Get the hosts defined in the specified slurm reservation.

    Args:
        log (logger): logger for the messages produced by this method
        partition (str): name of the slurm reservation from which to obtain the names

    Raises:
        SlurmFailed: if there is a problem obtaining the hosts from the slurm reservation

    Returns:
        NodeSet: slurm reservation hosts

    """
    if not reservation:
        return NodeSet()

    # Get the list of hosts from the reservation information
    result = show_reservation(log, control, reservation)
    if not result.passed:
        raise SlurmFailed(f'Unable to obtain hosts from the {reservation} slurm reservation')

    # Get the list of hosts from the reservation information
    try:
        output = '\n'.join(result.all_stdout.values())
        return NodeSet(','.join(re.findall(r'\sNodes=(\S+)', output)))
    except (NodeSetParseError, TypeError) as error:
        raise SlurmFailed(
            f'Unable to obtain hosts from the {reservation} slurm reservation output') from error


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
        uniq = random.randint(1, 100000)  # nosec
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
                if value is not None:
                    if key == "error":
                        value = value + str(uniq)
                    script_file.write("#SBATCH --{}={}\n".format(key, value))
                else:
                    script_file.write("#SBATCH --{}\n".format(key))
        script_file.write("\n")

        # debug
        script_file.write("echo \"nodes: \" $SLURM_JOB_NODELIST \n")
        script_file.write("echo \"node count: \" $SLURM_JOB_NUM_NODES \n")
        script_file.write("echo \"job name: \" $SLURM_JOB_NAME \n")

        for cmd in list(cmds):
            script_file.write(cmd + "\n")
        script_file.close()
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


def srun_str(hosts, cmd, srun_params=None, timeout=None):
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
    if timeout is not None:
        params_list.append("--time {}".format(timeout))
    if srun_params is not None:
        for key, value in list(srun_params.items()):
            params_list.extend(["--{}={}".format(key, value)])
            params = " ".join(params_list)
    cmd = "srun {} {}".format(params, cmd)
    return str(cmd)


def srun(hosts, cmd, srun_params=None, timeout=60):
    """Run srun cmd on slurm partition.

    Args:
        hosts (str): hosts to allocate
        cmd (str): cmdline to execute
        srun_params(dict): additional params for srun
        timeout

    Raises:
        SlurmFailed: if there is an error running the srun command

    Returns:
        CmdResult: object containing the result (exit status, stdout, etc.) of
            the srun command

    """
    srun_time = max(int(timeout / 60), 1)
    cmd = srun_str(hosts, cmd, srun_params, str(srun_time))
    try:
        result = run_command(cmd, timeout)
    except DaosTestError as error:
        result = None
        raise SlurmFailed("srun failed : {}".format(error)) from error
    return result


def install_slurm(log, hosts, sudo, timeout=600):
    """Install slurm packages.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to install slurm
        sudo (bool): whether or not to issue the commands with sudo.
        timeout (int, optional): command timeout in seconds. Defaults to 600.

    Returns:
        bool: True if slurm was installed successfully on all hosts

    """
    log.info('Installing packages on %s: %s', hosts, ', '.join(PACKAGES))
    sudo_command = ['sudo', '-n'] if sudo else []
    command = sudo_command + ['dnf', 'install', '-y'] + PACKAGES
    return run_remote(log, hosts, ' '.join(command), timeout=timeout).passed


def remove_slurm(log, hosts, sudo, timeout=600):
    """Remove slurm packages.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to remove slurm
        sudo (bool): whether or not to issue the commands with sudo.
        timeout (int, optional): command timeout in seconds. Defaults to 600.

    Returns:
        bool: True if slurm was removed successfully on all hosts

    """
    log.info('Removing packages on %s: %s', hosts, ', '.join(PACKAGES))
    sudo_command = ['sudo', '-n'] if sudo else []
    command = sudo_command + ['dnf', 'remove', '-y'] + PACKAGES
    return run_remote(log, hosts, ' '.join(command), timeout=timeout).passed


def slurm_installed(log, hosts):
    """Determine if slurm is installed on the specified hosts.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to determine if slurm is installed

    Returns:
        bool: True if all slurm packages are installed on all hosts

    """
    log.info('Determining if slurm is installed on %s', hosts)
    regex = ['\'(', '|'.join(PACKAGES), ')-[0-9]\'']
    command = ['rpm', '-qa', '|', 'grep', '-E', ''.join(regex)]
    result = run_remote(log, hosts, ' '.join(command))
    return result.homogeneous and len(result.output[0].stdout) >= len(PACKAGES)
