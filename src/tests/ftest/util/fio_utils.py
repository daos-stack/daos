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
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function
import general_utils

from command_utils import ExecutableCommand
from command_utils import CommandFailure, FormattedParameter, BasicParameter
from ClusterShell.NodeSet import NodeSet


class FioCommand(ExecutableCommand):
    """Defines a object representing a fio command."""

    def __init__(self, namespace, command, test):
        """Create a fio Command object."""
        super(FioCommand, self).__init__(namespace, command)
        self.test = test

        # fio commandline options
        self.debug = FormattedParameter("--debug={}")
        self.parse_only = FormattedParameter("--parse-only", False)
        self.output = FormattedParameter("--output={}")
        self.bandwidth_log = FormattedParameter("--bandwidth-log", False)
        self.minimal = FormattedParameter("minimal", False)
        self.output_format = FormattedParameter("--output-format={}")
        self.terse_version = FormattedParameter("--terse-version={}")
        self.version = FormattedParameter("--version", False)
        self.fio_help = FormattedParameter("--help", False)
        self.cpuclock_test = FormattedParameter("--cpuclock-test", False)
        self.crctest = FormattedParameter("--crctest={}")
        self.cmdhelp = FormattedParameter("--cmdhelp={}")
        self.enghelp = FormattedParameter("--enghelp={}")
        self.showcmd = FormattedParameter("--showcmd={}")
        self.eta = FormattedParameter("--eta={}")
        self.eta_newline = FormattedParameter("--eta-newline={}")
        self.status_interval = FormattedParameter("--status-interval={}")
        self.readonly = FormattedParameter("--readonly", False)
        self.section = FormattedParameter("--section={}")
        self.alloc_size = FormattedParameter("--alloc-size={}")
        self.warnings_fatal = FormattedParameter("--warnings-fatal", False)
        self.max_jobs = FormattedParameter("--max-jobs={}")
        self.server = FormattedParameter("--server={}")
        self.daemonize = FormattedParameter("--daemonize={}")
        self.client = FormattedParameter("--client={}")
        self.remote_config = FormattedParameter("--remote-config={}")
        self.idle_prof = FormattedParameter("--idle-prof={}")
        self.inflate_log = FormattedParameter("--inflate-log={}")
        self.trigger_file = FormattedParameter("--trigger-file={}")
        self.trigger_timeout = FormattedParameter("--trigger-timeout={}")
        self.trigger = FormattedParameter("--trigger={}")
        self.trigger_remote = FormattedParameter("--trigger-remote={}")
        self.aux_path = FormattedParameter("--aux-path={}")

        # fio job options
        self.description = FormattedParameter("--description={}")
        self.name = FormattedParameter("--name={}")
        self.wait_for = FormattedParameter("--wait_for={}")
        self.filename = FormattedParameter("--filename={}")
        self.lockfile = FormattedParameter("--lockfile={}")
        self.directory = FormattedParameter("--directory={}")
        self.filename_format = FormattedParameter("--filename_format={}")
        self.unique_filename = FormattedParameter("--unique_filename={}")
        self.opendir = FormattedParameter("--opendir={}")
        self.rw = FormattedParameter("--rw={}")
        self.blocksize = FormattedParameter("--bs={}")
        self.blockalign = FormattedParameter("--ba={}")
        self.bsrange = FormattedParameter("--bsrange={}")
        self.bssplit = FormattedParameter("--bssplit={}")
        self.bs_unaligned = FormattedParameter("--bs_unaligned", False)
        self.randrepeat = FormattedParameter("--randrepeat={}")
        self.randseed = FormattedParameter("--randseed={}")
        self.norandommap = FormattedParameter("--norandommap", False)
        self.ignore_error = FormattedParameter("--ignore_error={}")
        self.rw_sequencer = FormattedParameter("--rw_sequencer={}")
        self.ioengine = FormattedParameter("--ioengine={}")
        self.iodepth = FormattedParameter("--iodepth={}")
        self.iodepth_batch = FormattedParameter("--iodepth_batch={}")
        self.iodepth_batch_complete_min = FormattedParameter(
            "--iodepth_batch_complete_min={}")
        self.iodepth_batch_complete_max = FormattedParameter(
            "--iodepth_batch_complete_max={}")
        self.iodepth_low = FormattedParameter("--iodepth_low={}")
        self.serialize_overlap = FormattedParameter("--serialize_overlap={}")
        self.io_submit_mode = FormattedParameter("--io_submit_mode={}")
        self.size = FormattedParameter("--size={}")
        self.io_size = FormattedParameter("--io_size={}")
        self.fill_device = FormattedParameter("--fill_device={}")
        self.filesize = FormattedParameter("--filesize={}")
        self.file_append = FormattedParameter("--file_append={}")
        self.offset = FormattedParameter("--offset={}")
        self.offset_increment = FormattedParameter("--offset_increment={}")
        self.offset_align = FormattedParameter("--offset_align={}")
        self.number_ios = FormattedParameter("--number_ios={}")
        self.random_generator = FormattedParameter("--random_generator={}")
        self.random_distribution = FormattedParameter(
            "--random_distribution={}")
        self.percentage_random = FormattedParameter("--percentage_random={}")
        self.allrandrepeat = FormattedParameter("--allrandrepeat={}")
        self.nrfiles = FormattedParameter("--nrfiles={}")
        self.file_service_type = FormattedParameter("--file_service_type={}")
        self.openfiles = FormattedParameter("--openfiles={}")
        self.fallocate = FormattedParameter("--fallocate={}")
        self.fadvise_hint = FormattedParameter("--fadvise_hint={}")
        self.fsync = FormattedParameter("--fsync={}")
        self.fdatasync = FormattedParameter("--fdatasync={}")
        self.write_barrier = FormattedParameter("--write_barrier={}")
        self.sync_file_range = FormattedParameter("--sync_file_range={}")
        self.direct = FormattedParameter("--direct={}")
        self.atomic = FormattedParameter("--atomic={}")
        self.buffered = FormattedParameter("--buffered={}")
        self.sync = FormattedParameter("--sync={}")
        self.overwrite = FormattedParameter("--overwrite={}")
        self.loops = FormattedParameter("--loops={}")
        self.numjobs = FormattedParameter("--numjobs={}")
        self.startdelay = FormattedParameter("--startdelay={}")
        self.runtime = FormattedParameter("--runtime={}")
        self.time_based = FormattedParameter("--time_based={}")
        self.verify_only = FormattedParameter("--verify_only", False)
        self.ramp_time = FormattedParameter("--ramp_time={}")
        self.clocksource = FormattedParameter("--clocksource={}")
        self.mem = FormattedParameter("--mem={}")
        self.verify = FormattedParameter("--verify={}")
        self.do_verify = FormattedParameter("--do_verify={}")
        self.verifysort = FormattedParameter("--verifysort={}")
        self.verifysort_nr = FormattedParameter("--verifysort_nr={}")
        self.verify_interval = FormattedParameter("--verify_interval={}")
        self.verify_offset = FormattedParameter("--verify_offset={}")
        self.verify_pattern = FormattedParameter("--verify_pattern={}")
        self.verify_fatal = FormattedParameter("--verify_fatal={}")
        self.verify_dump = FormattedParameter("--verify_dump={}")
        self.verify_async = FormattedParameter("--verify_async={}")
        self.verify_backlog = FormattedParameter("--verify_backlog={}")
        self.verify_backlog_batch = FormattedParameter(
            "--verify_backlog_batch={}")
        self.experimental_verify = FormattedParameter(
            "--experimental_verify={}")
        self.verify_state_load = FormattedParameter("--verify_state_load={}")
        self.verify_state_save = FormattedParameter("--verify_state_save={}")
        self.trim_percentage = FormattedParameter("--trim_percentage={}")
        self.trim_verify_zero = FormattedParameter("--trim_verify_zero={}")
        self.trim_backlog = FormattedParameter("--trim_backlog={}")
        self.trim_backlog_batch = FormattedParameter("--trim_backlog_batch={}")
        self.write_iolog = FormattedParameter("--write_iolog={}")
        self.read_iolog = FormattedParameter("--read_iolog={}")
        self.replay_no_stall = FormattedParameter("--replay_no_stall={}")
        self.replay_redirect = FormattedParameter("--replay_redirect={}")
        self.replay_scale = FormattedParameter("--replay_scale={}")
        self.replay_align = FormattedParameter("--replay_align={}")
        self.exec_prerun = FormattedParameter("--exec_prerun={}")
        self.exec_postrun = FormattedParameter("--exec_postrun={}")
        self.ioscheduler = FormattedParameter("--ioscheduler={}")
        self.zonesize = FormattedParameter("--zonesize={}")
        self.zonerange = FormattedParameter("--zonerange={}")
        self.zoneskip = FormattedParameter("--zoneskip={}")
        self.lockmem = FormattedParameter("--lockmem={}")
        self.rwmixread = FormattedParameter("--rwmixread={}")
        self.rwmixwrite = FormattedParameter("--rwmixwrite={}")
        self.nice = FormattedParameter("--nice={}")
        self.prio = FormattedParameter("--prio={}")
        self.prioclass = FormattedParameter("--prioclass={}")
        self.thinktime = FormattedParameter("--thinktime={}")
        self.thinktime_spin = FormattedParameter("--thinktime_spin={}")
        self.thinktime_blocks = FormattedParameter("--thinktime_blocks={}")
        self.rate = FormattedParameter("--rate={}")
        self.rate_min = FormattedParameter("--rate_min={}")
        self.rate_process = FormattedParameter("--rate_process={}")
        self.rate_cycle = FormattedParameter("--rate_cycle={}")
        self.rate_ignore_thinktime = FormattedParameter(
            "--rate_ignore_thinktime={}")
        self.rate_iops = FormattedParameter("--rate_iops={}")
        self.rate_iops_min = FormattedParameter("--rate_iops_min={}")
        self.max_latency = FormattedParameter("--max_latency={}")
        self.latency_target = FormattedParameter("--latency_target={}")
        self.latency_window = FormattedParameter("--latency_window={}")
        self.latency_percentile = FormattedParameter("--latency_percentile={}")
        self.invalidate = FormattedParameter("--invalidate={}")
        self.write_hint = FormattedParameter("--write_hint={}")
        self.create_serialize = FormattedParameter("--create_serialize={}")
        self.create_fsync = FormattedParameter("--create_fsync={}")
        self.create_on_open = FormattedParameter("--create_on_open={}")
        self.create_only = FormattedParameter("--create_only={}")
        self.allow_file_create = FormattedParameter("--allow_file_create={}")
        self.allow_mounted_write = FormattedParameter(
            "--allow_mounted_write={}")
        self.pre_read = FormattedParameter("--pre_read={}")
        self.cpumask = FormattedParameter("--cpumask={}")
        self.cpus_allowed = FormattedParameter("--cpus_allowed={}")
        self.cpus_allowed_policy = FormattedParameter(
            "--cpus_allowed_policy={}")
        self.numa_cpu_nodes = FormattedParameter("--numa_cpu_nodes={}")
        self.numa_mem_policy = FormattedParameter("--numa_mem_policy={}")
        self.end_fsync = FormattedParameter("--end_fsync={}")
        self.fsync_on_close = FormattedParameter("--fsync_on_close={}")
        self.unlink = FormattedParameter("--unlink={}")
        self.unlink_each_loop = FormattedParameter("--unlink_each_loop={}")
        self.exitall = FormattedParameter("--exitall", False)
        self.exitall_on_error = FormattedParameter("--exitall_on_error", False)
        self.stonewall = FormattedParameter("--stonewall", False)
        self.new_group = FormattedParameter("--new_group", False)
        self.thread = FormattedParameter("--thread={}")
        self.per_job_logs = FormattedParameter("--per_job_logs={}")
        self.write_bw_log = FormattedParameter("--write_bw_log={}")
        self.bwavgtime = FormattedParameter("--bwavgtime={}")
        self.write_lat_log = FormattedParameter("--write_lat_log={}")
        self.write_iops_log = FormattedParameter("--write_iops_log={}")
        self.iopsavgtime = FormattedParameter("--iopsavgtime={}")
        self.log_avg_msec = FormattedParameter("--log_avg_msec={}")
        self.log_hist_msec = FormattedParameter("--log_hist_msec={}")
        self.log_hist_coarseness = FormattedParameter(
            "--log_hist_coarseness={}")
        self.write_hist_log = FormattedParameter("--write_hist_log={}")
        self.log_max_value = FormattedParameter("--log_max_value={}")
        self.log_offset = FormattedParameter("--log_offset={}")
        self.log_compression = FormattedParameter("--log_compression={}")
        self.log_compression_cpus = FormattedParameter(
            "--log_compression_cpus={}")
        self.log_store_compressed = FormattedParameter(
            "--log_store_compressed={}")
        self.log_unix_epoch = FormattedParameter("--log_unix_epoch={}")
        self.block_error_percentiles = FormattedParameter(
            "--block_error_percentiles={}")
        self.group_reporting = FormattedParameter("--group_reporting={}")
        self.stats = FormattedParameter("--stats={}")
        self.zero_buffers = FormattedParameter("--zero_buffers={}")
        self.refill_buffers = FormattedParameter("--refill_buffers={}")
        self.scramble_buffers = FormattedParameter("--scramble_buffers={}")
        self.buffer_pattern = FormattedParameter("--buffer_pattern={}")
        self.buffer_compress_percentage = FormattedParameter(
            "--buffer_compress_percentage={}")
        self.buffer_compress_chunk = FormattedParameter(
            "--buffer_compress_chunk={}")
        self.dedupe_percentage = FormattedParameter("--dedupe_percentage={}")
        self.clat_percentiles = FormattedParameter("--clat_percentiles={}")
        self.lat_percentiles = FormattedParameter("--lat_percentiles={}")
        self.percentile_list = FormattedParameter("--percentile_list={}")
        self.significant_figures = FormattedParameter(
            "--significant_figures={}")
        self.disk_util = FormattedParameter("--disk_util={}")
        self.gtod_reduce = FormattedParameter("--gtod_reduce={}")
        self.disable_lat = FormattedParameter("--disable_lat={}")
        self.disable_clat = FormattedParameter("--disable_clat={}")
        self.disable_slat = FormattedParameter("--disable_slat={}")
        self.disable_bw_measurement = FormattedParameter(
            "--disable_bw_measurement={}")
        self.gtod_cpu = FormattedParameter("--gtod_cpu={}")
        self.unified_rw_reporting = FormattedParameter(
            "--unified_rw_reporting={}")
        self.continue_on_error = FormattedParameter("--continue_on_error={}")
        self.error_dump = FormattedParameter("--error_dump={}")
        self.profile = FormattedParameter("--profile={}")
        self.cgroup = FormattedParameter("--cgroup={}")
        self.cgroup_nodelete = FormattedParameter("--cgroup_nodelete={}")
        self.cgroup_weight = FormattedParameter("--cgroup_weight={}")
        self.uid = FormattedParameter("--uid={}")
        self.gid = FormattedParameter("--gid={}")
        self.kb_base = FormattedParameter("--kb_base={}")
        self.unit_base = FormattedParameter("--unit_base={}")
        self.hugepage_size = FormattedParameter("--hugepage-size={}")
        self.flow_id = FormattedParameter("--flow_id={}")
        self.flow = FormattedParameter("--flow={}")
        self.flow_watermark = FormattedParameter("--flow_watermark={}")
        self.flow_sleep = FormattedParameter("--flow_sleep={}")
        self.steadystate = FormattedParameter("--steadystate={}")
        self.steadystate_duration = FormattedParameter(
            "--steadystate_duration={}")
        self.steadystate_ramp_time = FormattedParameter(
            "--steadystate_ramp_time={}")

        # fio api
        self.api = BasicParameter(None, "POSIX")

    def get_param_names(self):
        """Get a sorted list of the defined FioCommand parameters."""
        # Sort the FIO parameter names to generate consistent fio commands
        all_param_names = super(FioCommand, self).get_param_names()
        # move the 'name' option as the first option of fio job
        all_param_names.remove("name")
        all_param_names.insert(0, "name")

        return all_param_names


class Fio(FioCommand):
    """Class defining an object of type FioCommand"""

    def __init__(self, namespace, test):
        """Create a fio object"""
        # Get run command by iterating over different fio jobs
        # mentioned in yaml file under '/run/fio/'.
        self.run_cmd = ''
        for name in namespace:
            super(Fio, self).__init__(name, "fio", test)
            self.get_params(test)
            self.run_cmd += self.__str__()

    def run(self, host):
        """Execute Fio from the host passed on to the method.

           Args:
             host: hostname from where to launch fio
        """
        # run fio command
        self.run_cmd = self.run_cmd.replace("fio", ' ').replace(" POSIX", '')
        print("Running: {}".format('fio' + self.run_cmd))
        ret_code = general_utils.pcmd(host, 'fio' + self.run_cmd)

        # check for any failures
        if 0 not in ret_code:
            error_hosts = NodeSet(
                ",".join(
                    [str(node_set) for code, node_set in ret_code.items()
                     if code != 0]))
            raise CommandFailure(
                "Error starting fio on the following hosts: {}".format(
                    error_hosts))
