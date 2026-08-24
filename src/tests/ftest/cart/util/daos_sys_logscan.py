#!/usr/bin/env python3
#
# (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""Scan daos_engine log files to get a summary of pools activity."""

import argparse
import copy
import os
import re
import sys

import cart_logparse


class SysPools():
    """Directory of Pools and Summary Activities Found in Engine Log Files"""

    # Future possibilities include:
    # system map update events (output outside if pool-specific context)
    # SWIM events seen by PS leader?
    # add/remove target events on PS leader?
    # rebuild queued (PS leader)
    # rebuild scanning (PS leader warn about engine updates, #engines finishing scanning, stalls)
    # rebuild pulling (PS leader, #engines/may be a subset, starting/finishing pulling, stalls)
    # rebuild number of objects, records and progress made?
    # TODO: RBHUNG follow-on engine identification
    # When update_and_warn_for_slow_engines() reports "rank X not finished scanning!" or
    # "rank X not finished pulling!" immediately below the RBHUNG trigger, capture that list as
    # the actual engine set implicated by the hang. This is stronger than only reporting the
    # aggregate count and should be attached to the RBHUNG event in the scanner output.
    # TODO: follow-up engine inspection
    # After a reported RBHUNG, inspect the specific implicated engine logfile(s) in the narrow
    # window 2–3 minutes before the warning for nearby ERR/WARN lines that might explain the stall.
    # This should be used as a focused follow-up analysis when the PS leader log indicates that
    # only a subset of engines has not finished scanning or pulling.

    # Engine rank assignment and pool service leader step_up/down events
    re_rank_assign = re.compile(r"ds_mgmt_drpc_set_rank.*set rank to (\d+)")
    re_step_up = re.compile(r"rdb_raft_step_up.*([0-9a-fA-F]{8}).*leader of term (\d+)")
    re_step_down = re.compile(r"rdb_raft_step_down.*([0-9a-fA-F]{8}).*leader of term (\d+)")

    # Future possibility: target state change events
    # update_one_tgt(), update_tgt_down_drain_to_downout()
    #   "change Target.*rank (\d+) idx (\d+).*to (\w+)"
    #   need those functions to print pool UUID. Then here, store a list of target change events
    #   in
    #   tgt_change = {"rank": affected_rank, "tgt_idx": affected_tgtidx, "state": new_tgtstate}
    #   self._pools[puuid][term]["maps"][mapver]["tgt_state_changes"].append(tgt_change)

    # Rebuild admin start/stop lifecycle events.
    re_rb_admin_start = re.compile(r"([0-9a-fA-F]{8}): resume/start rebuild")
    re_rb_admin_stop = re.compile(
        r"rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(Rebuild|Reclaim|Reclaim fail): "
        r"(NOT stopping rebuild|stopping rebuild) force=(\d+) "
        r"opc (\d+)\(([^\)]+)\)(?:, rc=(-?\d+))?"
    )
    re_rb_admin_stop_none = re.compile(r"([0-9a-fA-F]{8}): nothing found to stop")

    # pool map version update events
    upd_re = r"ds_pool_tgt_map_update.*([0-9a-fA-F]{8}): updated.*map: version=(\d+)->(\d+) pointer"
    re_pmap_update = re.compile(upd_re)

    # System/group map update events: scope is initiator-first on a single engine stream.
    # Capture corpc initiation evidence, then consume matching handler-side update evidence
    # from that same stream (rank/pid/logfile).
    sys_corpc_init_re = re.compile(r"Received request to update group map with (\d+) ranks")
    sys_group_upd_re = re.compile(r"updated group:\s*(\d+)\s*->\s*(\d+):\s*(\d+)\s+ranks")
    sys_join_re = re.compile(
        r"(?:join(?:ed|ing)?|rejoin(?:ed|ing)?)\s+rank(?:s)?\s*[:=]?\s*([0-9,\-\s]+)",
        re.IGNORECASE)
    sys_excl_re = re.compile(
        r"(?:exclude(?:d|ing)?|evict(?:ed|ing)?)\s+rank(?:s)?\s*[:=]?\s*([0-9,\-\s]+)",
        re.IGNORECASE)

    # uniform rebuild string identifier rb=<pool_uuid>/<rebuild_ver>/<rebuild_gen>/<opcode_string>
    # need special "_rf" versions of regex's to match "Reclaim fail" op (as opposed to "Reclaim")
    rbid_re = r"rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(Rebuild|Reclaim)"
    rbid_rf_re = r"rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(Reclaim fail)"

    # Future possibility: match the rebuild preliminary steps
    # rebuild_task_ult() wait for scheduling, and map dist - both would info to match on.
    # re.compile(r"rebuild_task_ult\(\).*rebuild task sleep (\d+) second")
    # re.compile(r"rebuild_task_ult\(\).*map_dist_ver (\d+) map ver (\d+)")

    # Rebuild: PS leader engine starting and status checking a given operation
    # statuses: "scanning", "pulling", "completed", "aborted", "failed"
    ldr_start_re = "rebuild_leader_start.*" + rbid_re + "$"
    ldr_start_rf_re = "rebuild_leader_start.*" + rbid_rf_re + "$"

    ldr_status_re = r"rebuild_leader_status_check\(\).*" + rbid_re + r" \[(\w+)\]" + \
        r".*status (-?\d+)/(\d+) .*duration=(\d+)"
    ldr_status_rf_re = r"rebuild_leader_status_check\(\).*" + rbid_rf_re + r" \[(\w+)\]" + \
        r".*status (-?\d+)/(\d+) .*duration=(\d+)"
    ldr_scan_hung_re = r"update_and_warn_for_slow_engines\(\).*" + rbid_re + \
        r".*scan hung.*waiting for (\d+)/(\d+) engines"
    ldr_scan_hung_rf_re = r"update_and_warn_for_slow_engines\(\).*" + rbid_rf_re + \
        r".*scan hung.*waiting for (\d+)/(\d+) engines"
    ldr_pull_hung_re = r"update_and_warn_for_slow_engines\(\).*" + rbid_re + \
        r".*pull hung.*waiting for (\d+)/(\d+) engines"
    ldr_pull_hung_rf_re = r"update_and_warn_for_slow_engines\(\).*" + rbid_rf_re + \
        r".*pull hung.*waiting for (\d+)/(\d+) engines"
    re_rebuild_ldr_start = re.compile(ldr_start_re)
    re_rebuild_ldr_start_rf = re.compile(ldr_start_rf_re)

    re_rebuild_ldr_status = re.compile(ldr_status_re)
    re_rebuild_ldr_status_rf = re.compile(ldr_status_rf_re)
    re_rebuild_ldr_scan_hung = re.compile(ldr_scan_hung_re)
    re_rebuild_ldr_scan_hung_rf = re.compile(ldr_scan_hung_rf_re)
    re_rebuild_ldr_pull_hung = re.compile(ldr_pull_hung_re)
    re_rebuild_ldr_pull_hung_rf = re.compile(ldr_pull_hung_rf_re)

    # Legacy rebuild PS leader logging (before uniform rebuild string)
    old_ldr_start_re = \
        r"rebuild_leader_start.*([0-9a-fA-F]{8}).*version=(\d+)/(\d+).*op=(Rebuild|Reclaim)"
    old_ldr_start_rf_re = \
        r"rebuild_leader_start.*([0-9a-fA-F]{8}).*version=(\d+)/(\d+).*op=(Reclaim fail)"
    old_ldr_status_re = \
        (r"rebuild_leader_status_check\(\) (Rebuild|Reclaim) \[(\w+)\] \(pool ([0-9a-fA-F]{8}) "
         r"leader (\d+) term (\d+).*ver=(\d+),gen (\d+).*duration=(\d+) secs")
    old_ldr_status_rf_re = \
        (r"rebuild_leader_status_check\(\) (Reclaim fail) \[(\w+)\] \(pool ([0-9a-fA-F]{8}) "
         r"leader (\d+) term (\d+).*ver=(\d+),gen (\d+).*duration=(\d+) secs")
    re_old_ldr_start = re.compile(old_ldr_start_re)
    re_old_ldr_start_rf = re.compile(old_ldr_start_rf_re)
    re_old_ldr_status = re.compile(old_ldr_status_re)
    re_old_ldr_status_rf = re.compile(old_ldr_status_rf_re)

    def __init__(self):
        # dictionaries indexed by pool UUID
        self._pools = {}
        self._highest_pmapver = {}
        self._cur_ldr_rank = {}
        self._cur_ldr_pid = {}
        self._cur_term = {}

        # filename to rank map
        self._file_to_rank = {}
        self._rank_to_files = {}
        self._missing_fail_rank_warned = set()

        # System map/group-update tracking.
        self._system_events = []
        self._system_initiators = {}
        self._system_rank_ctx = {}

        self._warnings = []
        self._check_rb_new_fmt = True
        self._check_rb_legacy_fmt = True
        self._debug = False

        # other nested dictionaries within self._pools will be built-up
        # pool leadership terms dictionary indexed by integer term number
        # self._pools[puuid][term] -> {rank, begin_time, end_time, host, pid, logfile, maps={}}
        #
        # pool map versions dictionary indexed by integer pool map version
        # self._pools[puuid][term]["maps"][mapver] = {carryover, from_ver, time, rb_gens={}}
        #
        # rebuild generations dictionary indexed by integer rebuild generation number
        # contains: rebuild operation, start time, duration,
        #           status(started/completed/aborted/failed/scanning/pulling),
        #           and if any scan or pull hang warnings were logged by PS leader
        # self._pools[puuid][term]["maps"][mapver]["rb_gens"][gen] =
        #  {op, start_time, time, started, scanning, scan_hung, scan_hung_time, scan_num_eng_wait,
        #   pulling, pull_hung, pull_hung_time, pull_num_eng_wait, completed, aborted, failed, dur)

    def _create_term(self, puuid, term, rank, host, pid, logfile, begin_t="xx/xx-xx:xx:xx.xx",
                     end_t="xx/xx-xx:xx:xx.xx"):
        # carry over most recent map version into the new term, avoid later KeyError
        old_term = term - 1
        pmap_versions = {}
        if old_term in self._pools[puuid]:
            if self._pools and self._pools[puuid][old_term]["maps"] != {}:
                last_mapver = max(self._pools[puuid][old_term]["maps"].keys())
                pmap_versions[last_mapver] = copy.deepcopy(
                    self._pools[puuid][old_term]["maps"][last_mapver]
                )
                pmap_versions[last_mapver]["carryover"] = True
                pmap_versions[last_mapver]["source"] = "carryover"

        return {
            "rank": rank,
            "begin_time": begin_t,
            "end_time": end_t,
            "host": host,
            "pid": pid,
            "logfile": logfile,
            "admin_events": [],
            "maps": pmap_versions
        }

    def _create_mapver(self, ver, carryover=False, tm="xx/xx-xx:xx:xx.xx", source="placeholder"):
        return {
            "carryover": carryover,
            "from_ver": ver,
            "time": tm,
            "source": source,
            "rb_gens": {}
        }

    def _create_rbgen(self, op, start_time="xx/xx-xx:xx:xx.xx"):
        return {
            "op": op,
            "start_time": start_time,
            "time": "xx/xx-xx:xx:xx.xx",
            "started": True,
            "scanning": False,
            "scan_hung": False,
            "scan_hung_time": "xx/xx-xx:xx:xx.xx",
            "scan_num_eng_wait": 0,
            "pulling": False,
            "pull_hung": False,
            "pull_hung_time": "xx/xx-xx:xx:xx.xx",
            "pull_num_eng_wait": 0,
            "completed": False,
            "aborted": False,
            "failed": False,
            "fail_rank": -1,
            "rc": 0,
            "duration": 0,
            "rbterr_checked": False,
            "rbterr_time": "",
            "rbterr_logfile": ""
        }

    def _build_rank_to_files(self):
        rank_to_files = {}
        for fname, rank in self._file_to_rank.items():
            if rank < 0:
                continue
            rank_to_files.setdefault(rank, []).append(fname)

        for rank in rank_to_files:
            rank_to_files[rank] = sorted(rank_to_files[rank])

        self._rank_to_files = rank_to_files

    @staticmethod
    def _stream_key(fname, pid, rank):
        return fname, pid, rank

    def _capture_system_rank_context(self, stream_key, msg, datetime):
        action = None
        ranklist = "-"

        match = self.sys_join_re.search(msg)
        if match:
            action = "joined"
            ranklist = " ".join(match.group(1).split())

        match = self.sys_excl_re.search(msg)
        if match:
            action = "excluded"
            ranklist = " ".join(match.group(1).split())

        if action:
            self._system_rank_ctx[stream_key] = {
                "time": datetime,
                "action": action,
                "ranklist": ranklist if ranklist else "-",
            }

    def _capture_system_initiator(self, stream_key, msg, datetime, fname, host, pid, rank):
        match = self.sys_corpc_init_re.search(msg)
        if not match:
            return

        self._system_initiators[stream_key] = {
            "time": datetime,
            "token": self._ts_sort_token(datetime),
            "req_ranks": int(match.group(1)),
            "fname": fname,
            "host": host,
            "pid": pid,
            "rank": rank,
        }

    def _capture_system_transition(self, stream_key, msg, datetime):
        match = self.sys_group_upd_re.search(msg)
        if not match:
            return

        # Initiator-first gating: do not accept transition lines unless this stream
        # has prior initiator evidence.
        init = self._system_initiators.get(stream_key)
        if init is None:
            return

        event_token = self._ts_sort_token(datetime)
        init_token = init["token"]
        if event_token is None or init_token is None:
            return
        if event_token < init_token:
            return

        from_ver = int(match.group(1))
        to_ver = int(match.group(2))
        total_ranks = int(match.group(3))

        action = "unknown"
        ranklist = "-"
        rank_ctx = self._system_rank_ctx.get(stream_key)
        if rank_ctx:
            rank_ctx_token = self._ts_sort_token(rank_ctx["time"])
            if rank_ctx_token is not None and rank_ctx_token <= event_token:
                action = rank_ctx["action"]
                ranklist = rank_ctx["ranklist"]

        self._system_events.append({
            "time": datetime,
            "token": event_token,
            "from_ver": from_ver,
            "to_ver": to_ver,
            "total_ranks": total_ranks,
            "action": action,
            "ranklist": ranklist,
            "fname": init["fname"],
            "rank": init["rank"],
            "pid": init["pid"],
            "host": init["host"],
        })

    def _capture_system_events(self, fname, line, pid, rank):
        msg, host, datetime = self._get_line_components(line)
        stream_key = self._stream_key(fname, pid, rank)
        self._capture_system_rank_context(stream_key, msg, datetime)
        self._capture_system_initiator(stream_key, msg, datetime, fname, host, pid, rank)
        self._capture_system_transition(stream_key, msg, datetime)

    @property
    def system_events(self):
        """Return all captured system map/group transitions."""
        return self._system_events

    def print_system(self):
        """Print system map/group update report before pool reports."""
        print(f"\n======== System Report ({len(self._system_events)} transitions) ========\n")

        for event in sorted(self._system_events,
                            key=lambda evt: (evt["token"], evt["from_ver"], evt["to_ver"])):
            print(f"{event['time']} SYSTEM MAPVER {event['from_ver']}->{event['to_ver']} "
                  f"ranks={event['total_ranks']} {event['action']} {event['ranklist']} "
                  f"{event['fname']}")

    def _probe_fail_rank_err(self, fail_rank, rc, start_time, end_time):
        """Find earliest in-window ERR line on fail_rank containing matching negative rc."""
        files = self._rank_to_files.get(fail_rank, [])
        if not files:
            if fail_rank not in self._missing_fail_rank_warned:
                self._warn(f"no logfile found for fail_rank={fail_rank}", "<all-files>")
                self._missing_fail_rank_warned.add(fail_rank)
            return "", ""

        start_token = self._ts_sort_token(start_time)
        end_token = self._ts_sort_token(end_time)
        if start_token is None or end_token is None:
            return "", ""

        # Target engine logs usually report negative errno values.
        target_err = -abs(rc)
        target_re = re.compile(rf"(?<!\\d){target_err}(?!\\d)")
        best_token = None
        best_time = ""
        best_file = ""

        for fname in files:
            try:
                log_iter = cart_logparse.LogIter(fname)
            except UnicodeDecodeError:
                log_iter = cart_logparse.LogIter(fname, check_encoding=True)

            if log_iter.file_corrupt:
                continue

            file_match_time = ""
            file_match_token = None
            for line in log_iter.new_iter():
                if line.level != cart_logparse.LOG_LEVELS["ERR"]:
                    continue

                line_time = line.time_stamp
                line_token = self._ts_sort_token(line_time)
                if line_token is None:
                    continue
                if line_token < start_token or line_token > end_token:
                    continue

                if not target_re.search(line.get_msg()):
                    continue

                file_match_time = line_time
                file_match_token = line_token
                break

            if file_match_token is not None:
                if best_token is None or file_match_token < best_token:
                    best_token = file_match_token
                    best_time = file_match_time
                    best_file = fname

        return best_time, best_file

    def _warn(self, wmsg, fname, line=None):
        full_msg = f"WARN file={fname}"
        if line:
            full_msg += f", line={line.lineno}"
        full_msg += f": {wmsg}"

        self._warnings.append(full_msg)
        print(full_msg)

    def _ts_sort_token(self, tstr):
        """Convert timestamp text into a sortable token, or None if unavailable."""
        if not tstr:
            return None

        # Expected examples include: YYYY/MM/DD HH:MM:SS.mmmmmm
        match = re.match(
            r"^(\d{4})/(\d{2})/(\d{2})\s+(\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?$",
            tstr,
        )
        if match:
            micro = match.group(7) or "0"
            return (
                int(match.group(1)),
                int(match.group(2)),
                int(match.group(3)),
                int(match.group(4)),
                int(match.group(5)),
                int(match.group(6)),
                int(micro.ljust(6, "0")[:6]),
            )

        # Legacy-like fallback: MM/DD-HH:MM:SS(.uuuuuu)
        match = re.match(
            r"^(\d{2})/(\d{2})-(\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?$",
            tstr,
        )
        if match:
            micro = match.group(6) or "0"
            return (
                0,
                int(match.group(1)),
                int(match.group(2)),
                int(match.group(3)),
                int(match.group(4)),
                int(match.group(5)),
                int(micro.ljust(6, "0")[:6]),
            )

        return None

    def _sort_term_events(self, events):
        """Sort term events by timestamp, then rebuild generation, then stable insertion index."""
        def _event_key(event):
            token = self._ts_sort_token(event["time"])
            if token is None:
                return (1, (), event["gen"], event["idx"])
            return (0, token, event["gen"], event["idx"])

        return sorted(events, key=_event_key)

    def _check_monotonic_token(self, last_token, this_token):
        """Return True if timestamps are monotonic when both tokens are present."""
        if last_token is None or this_token is None:
            return True
        return this_token >= last_token

    @property
    def warnings(self):
        """Return all warnings stored when scanning engine log files"""
        return self._warnings

    def _set_rank(self, log_iter):
        print(f"INFO: searching for rank in file {log_iter.fname}")
        for line in log_iter.new_iter():
            # when a rank assignment log line found (engine start)
            match = self.re_rank_assign.match(line.get_msg())
            if match:
                self._file_to_rank[log_iter.fname] = int(match.group(1))
                return True

            # Future enhancement: what about log rotation (not an engine start scenario)?
        return False

    # return log-message, hostname, and date/timestamp components of the line
    def _get_line_components(self, line):
        return line.get_msg(), line.hostname, line.time_stamp

    # is this rank, pid the leader of the pool with uuid puuid?
    def _is_leader(self, puuid, rank, pid):
        if puuid not in self._pools:
            return False
        if self._cur_ldr_rank[puuid] == rank and self._cur_ldr_pid[puuid] == pid:
            return True
        return False

    def _get_ps_leader_components(self, match):
        # puuid, term
        # see re_step_up and re_step_down
        return match.group(1), int(match.group(2))

    def _match_ps_step_up(self, fname, line, pid, rank):
        msg, host, datetime = self._get_line_components(line)
        match = self.re_step_up.match(msg)
        if not match:
            return False

        puuid, term = self._get_ps_leader_components(match)
        if puuid not in self._pools:
            self._pools[puuid] = {}
        self._cur_ldr_rank[puuid] = rank
        self._cur_ldr_pid[puuid] = pid
        self._cur_term[puuid] = term
        # if term already exists, error?
        if term in self._pools[puuid]:
            self._warn(f"pool {puuid} term {term} already seen!", fname, line)
        self._pools[puuid][term] = self._create_term(puuid, term, rank, host, pid, fname,
                                                     begin_t=datetime)
        if self._debug:
            print(f"{datetime} FOUND pool {puuid} BEGIN\tterm {term} rank {rank}\t{host}"
                  f"\tPID {pid}\t{fname}")
        return True

    def _match_ps_step_down(self, fname, line, pid, rank):
        msg, host, datetime = self._get_line_components(line)
        match = self.re_step_down.match(msg)
        if not match:
            return False

        puuid, term = self._get_ps_leader_components(match)
        if puuid not in self._pools:
            self._pools[puuid] = {}
            self._cur_term[puuid] = -1

        if term != self._cur_term[puuid]:
            self._warn(f"pool {puuid} step_down term={term} != cur_term={self._cur_term[puuid]}",
                       fname, line)
        if term not in self._pools[puuid]:
            self._pools[puuid][term] = self._create_term(puuid, term, rank, host, pid, fname,
                                                         end_t=datetime)
        else:
            self._pools[puuid][term]["end_time"] = datetime

        self._cur_ldr_rank[puuid] = -1
        self._cur_ldr_pid[puuid] = -1
        self._cur_term[puuid] = -1
        if self._debug:
            print(f"{datetime} FOUND pool {puuid} END\tterm {term} rank {rank}\t{host}\t"
                  f"PID {pid}\t{fname}")
        return True

    def _get_pmap_update_components(self, match):
        # puuid, from_version, to_version
        # see re_pmap_update
        return match.group(1), int(match.group(2)), int(match.group(3))

    def _match_ps_pmap_update(self, fname, line, pid, rank):
        msg, host, datetime = self._get_line_components(line)
        match = self.re_pmap_update.match(msg)
        if not match:
            return False

        puuid, from_ver, to_ver = self._get_pmap_update_components(match)
        # ignore if this engine is not the leader
        if not self._is_leader(puuid, rank, pid):
            return True
        term = self._cur_term[puuid]
        self._pools[puuid][term]["maps"][to_ver] = \
            self._create_mapver(from_ver, carryover=False, tm=datetime, source="update")
        if self._debug:
            print(f"FOUND pool {puuid} map update {from_ver}->{to_ver} rank {rank}\t{host}\t"
                  f"PID {pid}\t{fname}")
        return True

    def _match_ps_rb_admin(self, fname, line, pid, rank):
        msg, host, datetime = self._get_line_components(line)

        match = self.re_rb_admin_start.match(msg)
        if match:
            puuid = match.group(1)
            if puuid in self._pools and self._is_leader(puuid, rank, pid):
                term = self._cur_term.get(puuid, -1)
                if term in self._pools[puuid]:
                    self._pools[puuid][term]["admin_events"].append({
                        "time": datetime,
                        "token": self._ts_sort_token(datetime),
                        "code": "STRTRQ",
                        "text": f"{datetime} {puuid} STRTRQ term={term} rank={rank} "
                                f"{host}\tPID {pid}\t{fname}",
                    })
            return True

        match = self.re_rb_admin_stop.match(msg)
        if match:
            puuid = match.group(1)
            if puuid not in self._pools or not self._is_leader(puuid, rank, pid):
                return True

            term = self._cur_term.get(puuid, -1)
            if term not in self._pools[puuid]:
                return True

            ver = int(match.group(2))
            gen = int(match.group(3))
            op = match.group(4)
            stop_action = match.group(5)
            force = int(match.group(6))
            opc_name = match.group(8)
            rc_str = match.group(9)
            rc = int(rc_str) if rc_str is not None else 0

            if rc != 0:
                code = "STOPRJ"
                suffix = f"rejected rc={rc}"
            else:
                code = "STOPRQ"
                suffix = f"requested force={force} opc={opc_name}"
                if stop_action.startswith("NOT"):
                    suffix += " deferred"

            self._pools[puuid][term]["admin_events"].append({
                "time": datetime,
                "token": self._ts_sort_token(datetime),
                "code": code,
                "text": f"{datetime} {puuid} {code} {ver}/{gen}/{op} {suffix} "
                        f"rank={rank} {host}\tPID {pid}\t{fname}",
                "ver": ver,
                "gen": gen,
            })
            return True

        match = self.re_rb_admin_stop_none.match(msg)
        if match:
            puuid = match.group(1)
            if puuid in self._pools and self._is_leader(puuid, rank, pid):
                term = self._cur_term.get(puuid, -1)
                if term in self._pools[puuid]:
                    self._pools[puuid][term]["admin_events"].append({
                        "time": datetime,
                        "token": self._ts_sort_token(datetime),
                        "code": "STOPRJ",
                        "text": f"{datetime} {puuid} STOPRJ no-running-rebuild rank={rank} "
                                f"{host}\tPID {pid}\t{fname}",
                    })
            return True

        return False

    def _get_rb_components(self, match):
        # puuid, map version number, rebuild generation number, rebuild operation string
        # same for new uniform identifier format and legacy log line format
        # see re_rebuild_ldr_start, re_old_ldr_start
        return match.group(1), int(match.group(2)), int(match.group(3)), match.group(4)

    def _match_ps_rb_start(self, fname, line, pid, rank):
        # Do not match on new rebuild log format if we found legacy format
        if not self._check_rb_new_fmt:
            return False
        msg, host, datetime = self._get_line_components(line)
        match = self.re_rebuild_ldr_start_rf.match(msg)
        if not match:
            match = self.re_rebuild_ldr_start.match(msg)
            if not match:
                return False

        # Disable checking for legacy rebuild log format, to save execution time
        self._check_rb_legacy_fmt = False
        puuid, ver, gen, op = self._get_rb_components(match)
        if not self._is_leader(puuid, rank, pid):
            return True
        term = self._cur_term[puuid]
        if term < 1:
            self._warn(f"pool {puuid} I don't know what term it is ({term})!", fname, line)
            return True

        if ver not in self._pools[puuid][term]["maps"]:
            self._warn(f"pool {puuid} term {term} ver {ver} not in maps - add placeholder",
                       fname, line)
            self._pools[puuid][term]["maps"][ver] = self._create_mapver(ver)

        if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
            self._warn(f"pool {puuid} term {term} ver {ver} already has gen {gen}", fname, line)
        # Future possibility: keep timestamps, durations for scan start, pull start, completed
        self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = \
            self._create_rbgen(op, start_time=datetime)
        if self._debug:
            print(f"{datetime} FOUND rebuild start in term {term}, rb={puuid}/{ver}/{gen}/{op} "
                  f"rank {rank}\t{host}\tPID {pid}\t{fname}")
        return True

    def _match_legacy_ps_rb_start(self, fname, line, pid, rank):
        # Do not match on legacy rebuild log format if we found new format
        if not self._check_rb_legacy_fmt:
            return False
        msg, host, datetime = self._get_line_components(line)
        match = self.re_old_ldr_start_rf.match(msg)
        if not match:
            match = match = self.re_old_ldr_start.match(msg)
            if not match:
                return False

        # Disable checking for new rebuild log format, to save execution time
        self._check_rb_new_fmt = False
        puuid, ver, gen, op = self._get_rb_components(match)
        if not self._is_leader(puuid, rank, pid):
            return True
        term = self._cur_term[puuid]
        if term < 1:
            self._warn(f"pool {puuid} I don't know what term it is ({term})!", fname, line)
            return True
        if self._debug:
            print(f"{datetime} FOUND rebuild start in term {term}, rb={puuid}/{ver}/{gen}/{op} "
                  f"rank {rank}\t{host}\tPID {pid}\t{fname}")

        if ver not in self._pools[puuid][term]["maps"]:
            self._warn(f"pool {puuid} term {term} ver {ver} not in maps - add placeholder",
                       fname, line)
            self._pools[puuid][term]["maps"][ver] = self._create_mapver(ver)

        if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
            self._warn(f"pool {puuid} term {term} ver {ver} already has gen {gen}", fname, line)
        self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = \
            self._create_rbgen(op, start_time=datetime)

        if self._debug:
            print(f"{datetime} SET _pools[{puuid}][{term}][maps][{ver}][rb_gens][{gen}]: {op}")

        return True

    def _get_ps_rb_status_components(self, match):
        # puuid, map version, rebuild-generation, operation, status, rc, fail_rank, duration
        # see re_rebuild_ldr_status
        return self._get_rb_components(match) + (match.group(5), int(match.group(6)),
                                                 int(match.group(7)), int(match.group(8)))

    def _match_ps_rb_status(self, fname, line, pid, rank):
        # Do not match on new rebuild log format if we found legacy format
        if not self._check_rb_new_fmt:
            return False
        msg, host, datetime = self._get_line_components(line)
        match = self.re_rebuild_ldr_status_rf.match(msg)
        if not match:
            match = self.re_rebuild_ldr_status.match(msg)
            if not match:
                return False

        # Disable checking for legacy rebuild log format, to save execution time
        self._check_rb_legacy_fmt = False
        puuid, ver, gen, op, status, rc, fail_rank, dur = self._get_ps_rb_status_components(match)
        if not self._is_leader(puuid, rank, pid):
            return True
        term = self._cur_term[puuid]
        if term < 1:
            self._warn(f"pool {puuid} I don't know what term it is ({term})!", fname, line)
            return True
        if ver not in self._pools[puuid][term]["maps"]:
            self._warn(f"pool {puuid} term {term} ver {ver} not in maps - add placeholder",
                       fname, line)
            self._pools[puuid][term]["maps"][ver] = self._create_mapver(ver)
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = self._create_rbgen(op)

        if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
            existing_op = self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["op"]
            if op != existing_op:
                self._warn(f"rb={puuid}/{ver}/{gen}/{existing_op} != line op {op}", fname, line)
        else:
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = self._create_rbgen(op)

        if status == "scanning":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["scanning"] = True
        elif status == "pulling":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["pulling"] = True
        elif status == "completed":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["completed"] = True
        elif status == "aborted":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["aborted"] = True
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["fail_rank"] = fail_rank
        elif status == "failed":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["failed"] = True
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["fail_rank"] = fail_rank

        self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["time"] = datetime
        self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["rc"] = rc
        self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["duration"] = dur
        if self._debug:
            print(f"{datetime} FOUND rebuild UPDATE term={term} rb={puuid}/{ver}/{gen}/{op} "
                  f"STATUS={status}, rc={rc}, fail_rank={fail_rank}, DUR={dur} seconds "
                  f"rank {rank}\t{host}\tPID {pid}\t{fname}")
        return True

    def _get_legacy_ps_rb_status_components(self, match):
        # rebuild-op, status, puuid, leader rank, term, map version, rebuild-gen, duration
        # see re_old_ldr_status
        return match.group(1), match.group(2), match.group(3), int(match.group(4)), \
            int(match.group(5)), int(match.group(6)), int(match.group(7)), int(match.group(8))

    def _match_legacy_ps_rb_status(self, fname, line, pid, rank):
        # Do not match on legacy rebuild log format if we found new format
        if not self._check_rb_legacy_fmt:
            return False
        msg, host, datetime = self._get_line_components(line)
        match = self.re_old_ldr_status_rf.match(msg)
        if not match:
            match = self.re_old_ldr_status.match(msg)
            if not match:
                return False

        # Disable checking for new rebuild log format, to save execution time
        self._check_rb_new_fmt = False
        op, status, puuid, log_ldr, log_term, ver, gen, dur = \
            self._get_legacy_ps_rb_status_components(match)
        if not self._is_leader(puuid, rank, pid):
            return True
        if rank != log_ldr:
            self._warn(f"pool {puuid} my rank {rank} != leader {log_ldr}", fname, line)
        term = self._cur_term[puuid]
        if term < 1 or term != log_term:
            self._warn(f"pool {puuid} I don't know what term it is ({term}), {log_term}!",
                       fname, line)
            return True
        if ver not in self._pools[puuid][term]["maps"]:
            self._warn(f"pool {puuid} term {term} ver {ver} not in maps - add placeholder",
                       fname, line)
            self._pools[puuid][term]["maps"][ver] = self._create_mapver(ver)
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = self._create_rbgen(op)

        if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
            existing_op = self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["op"]
            if op != existing_op:
                self._warn(f"rb={puuid}/{ver}/{gen}/{existing_op} != line op {op}", fname, line)
        else:
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = self._create_rbgen(op)

        if status == "scanning":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["scanning"] = True
        elif status == "pulling":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["pulling"] = True
        elif status == "completed":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["completed"] = True
        elif status == "aborted":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["aborted"] = True
        elif status == "failed":
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["failed"] = True
        self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["time"] = datetime
        self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["duration"] = dur
        if self._debug:
            print(f"{datetime} FOUND rebuild UPDATE term={term} rb={puuid}/{ver}/{gen}/{op} "
                  f"STATUS={status}, DUR={dur} seconds rank {rank}\t{host}\tPID {pid}\t{fname}")
        return True

    def _get_ps_rb_hung_warn_components(self, match):
        # puuid, map version, rebuild-generation, operation, status, duration
        # see re_rebuild_ldr_scan_hung and re_rebuild_ldr_pull_hung
        return self._get_rb_components(match) + (int(match.group(5)), int(match.group(6)))

    def _match_ps_rb_hung_warn(self, fname, line, pid, rank):
        msg, host, datetime = self._get_line_components(line)
        match1 = self.re_rebuild_ldr_scan_hung_rf.match(msg)
        if not match1:
            match1 = self.re_rebuild_ldr_scan_hung.match(msg)
        match2 = self.re_rebuild_ldr_pull_hung_rf.match(msg)
        if not match2:
            match2 = self.re_rebuild_ldr_pull_hung.match(msg)
        if not match1 and not match2:
            return False

        if match1:
            puuid, ver, gen, op, compl_eng, tot_eng = self._get_ps_rb_hung_warn_components(match1)
            term = self._cur_term[puuid]
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["scan_hung"] = True
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["scan_num_eng_wait"] = compl_eng
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["scan_hung_time"] = datetime
            if self._debug:
                print(f"{datetime} FOUND rebuild SCAN hung term={term} rb={puuid}/{ver}/{gen}/{op} "
                      f"{compl_eng} / {tot_eng} done, rank {rank}\t{host}\tPID {pid}\t{fname}")

        if match2:
            puuid, ver, gen, op, compl_eng, tot_eng = self._get_ps_rb_hung_warn_components(match2)
            term = self._cur_term[puuid]
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["pull_hung"] = True
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["pull_num_eng_wait"] = compl_eng
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["pull_hung_time"] = datetime
            if self._debug:
                print(f"{datetime} FOUND rebuild PULL hung term={term} rb={puuid}/{ver}/{gen}/{op} "
                      f"{compl_eng} / {tot_eng} done, rank {rank}\t{host}\tPID {pid}\t{fname}")

        return True

    def scan_file(self, log_iter, rank=-1):
        """Scan a daos engine log file and insert important pool events into a nested dictionary"""
        fname = log_iter.fname

        # Find rank assignment log line for this file. Can't do much without it.
        self._file_to_rank[fname] = rank
        if rank == -1 and not self._set_rank(log_iter):
            self._warn("cannot find rank assignment in log file - skipping", fname)
            return
        rank = self._file_to_rank[fname]

        for pid in log_iter.get_pids():
            print(f"INFO: scanning file {fname} rank {rank}, PID {pid}")
            for line in log_iter.new_iter(pid=pid):
                # System map/group events: initiator-first and single-stream scoped.
                self._capture_system_events(fname, line, pid, rank)

                # Find pool term begin (PS leader step_up)
                if self._match_ps_step_up(fname, line, pid, rank):
                    continue

                # Find pool term end (PS leader step_down)
                if self._match_ps_step_down(fname, line, pid, rank):
                    continue

                # Find pool map updates
                if self._match_ps_pmap_update(fname, line, pid, rank):
                    continue

                # Find rebuild admin start/stop lifecycle events by the PS leader
                if self._match_ps_rb_admin(fname, line, pid, rank):
                    continue

                # Find rebuild start by the PS leader
                if self._match_ps_rb_start(fname, line, pid, rank):
                    continue

                if self._match_legacy_ps_rb_start(fname, line, pid, rank):
                    continue

                # Find rebuild status updates
                if self._match_ps_rb_status(fname, line, pid, rank):
                    continue

                if self._match_legacy_ps_rb_status(fname, line, pid, rank):
                    continue

                # Find rebuild scan or pull phase hung warnings
                if self._match_ps_rb_hung_warn(fname, line, pid, rank):
                    continue

            # Future: for a PID that is killed, clear any associated cur_ldr_rank / cur_ldr_pid.
            # At logfile end, it could be due to engine killed, or could just be log rotation.

    def print_pools(self):
        # pylint: disable=too-many-locals
        # pylint: disable=too-many-nested-blocks
        """Print all pools important events found in a nested dictionary"""

        # pd (pool dictionary): pool UUID -> td
        # td (term dictionary): term number -> "maps" md
        # md (map dictionary): pool map version number -> "rb_gens" rd
        # rd (rebuild dictionary): rebuild generation number -> rebuild operation details
        for puuid, pd in self._pools.items():
            print(f"===== Pool {puuid}:")
            last_seen_token = None
            pool_has_time_out_of_order = False
            line_idx = 0

            def emit_report_line(line_time, line_text):
                nonlocal last_seen_token
                nonlocal pool_has_time_out_of_order
                token = self._ts_sort_token(line_time)
                if not self._check_monotonic_token(last_seen_token, token):
                    pool_has_time_out_of_order = True
                if token is not None:
                    last_seen_token = token
                print(line_text)

            for term, td in pd.items():
                b = td["begin_time"]
                e = td["end_time"]
                r = td["rank"]
                h = td["host"]
                p = td["pid"]
                f = td["logfile"]
                term_events = []

                term_events.append({
                    "time": b,
                    "gen": -1,
                    "idx": line_idx,
                    "text": f"{b} {puuid} BEGIN  term {term}\trank {r}\t{h}\tPID {p}\t{f}",
                })
                line_idx += 1

                for admin_evt in td.get("admin_events", []):
                    term_events.append({
                        "time": admin_evt["time"],
                        "gen": -1,
                        "idx": line_idx,
                        "text": admin_evt["text"],
                    })
                    line_idx += 1

                for v, md in td["maps"].items():
                    # Future: print tgt state changes before corresponding map updates

                    # Report map versions that are real updates or explicit carryovers.
                    if md.get("source") == "update" or md.get("carryover"):
                        t = md["time"]
                        from_ver = md["from_ver"]
                        term_events.append({
                            "time": t,
                            "gen": -1,
                            "idx": line_idx,
                            "text": (f"{t} {puuid} MAPVER {from_ver}->{v} "
                                     f"source={md.get('source', 'unknown')}"),
                        })
                        line_idx += 1

                    # Rebuild-specific lines: generation order naturally follows timestamp ordering,
                    # with generation as a deterministic tie-breaker.
                    for g in sorted(md["rb_gens"].keys()):
                        rd = md["rb_gens"][g]
                        op = rd["op"]
                        dur = rd["duration"]
                        scan = rd["scanning"]
                        scan_hung = rd["scan_hung"]
                        scan_num_eng_wait = rd["scan_num_eng_wait"]
                        pull = rd["pulling"]
                        pull_hung = rd["pull_hung"]
                        pull_num_eng_wait = rd["pull_num_eng_wait"]
                        comp = rd["completed"]
                        abrt = rd["aborted"]
                        fail = rd["failed"]
                        fail_rank = rd["fail_rank"]
                        rc = rd["rc"]
                        st = rd["start_time"]
                        ut = rd["time"]

                        # status is latest status reached for the given rebuild
                        status = "started"
                        if abrt:
                            status = "aborted"
                        elif comp:
                            status = "completed"
                        elif fail:
                            status = "failed"
                        elif pull:
                            status = "pulling"
                        elif scan:
                            status = "scanning"

                        # hung_status
                        hung_status = ""
                        if scan_hung:
                            hung_status += "scan-hung"
                            scan_hung_time = rd["scan_hung_time"]
                        if pull_hung:
                            if hung_status != "":
                                hung_status += ","
                            hung_status += "pull-hung"
                            pull_hung_time = rd["pull_hung_time"]

                        term_events.append({
                            "time": st,
                            "gen": g,
                            "idx": line_idx,
                            "text": f"{st} {puuid} RBSTRT {v}/{g}/{op}",
                        })
                        line_idx += 1

                        if scan_hung:
                            term_events.append({
                                "time": scan_hung_time,
                                "gen": g,
                                "idx": line_idx,
                                "text": (
                                    f"{scan_hung_time} {puuid} RBHUNG {v}/{g}/{op} "
                                    f"{hung_status}: {scan_num_eng_wait} engines not done scanning"
                                ),
                            })
                            line_idx += 1
                        if pull_hung:
                            term_events.append({
                                "time": pull_hung_time,
                                "gen": g,
                                "idx": line_idx,
                                "text": (
                                    f"{pull_hung_time} {puuid} RBHUNG {v}/{g}/{op} "
                                    f"{hung_status}: {pull_num_eng_wait} engines not done pulling"
                                ),
                            })
                            line_idx += 1
                        if scan or pull or comp or abrt or fail:
                            term_events.append({
                                "time": ut,
                                "gen": g,
                                "idx": line_idx,
                                "text": f"{ut} {puuid} RBUPDT {v}/{g}/{op} {status} rc={rc} "
                                        f"fail_rank={fail_rank} {dur} seconds",
                            })
                            line_idx += 1

                        if abrt and rc != 0 and fail_rank != -1:
                            if not rd["rbterr_checked"]:
                                err_time, err_file = self._probe_fail_rank_err(
                                    fail_rank, rc, st, ut)
                                rd["rbterr_time"] = err_time
                                rd["rbterr_logfile"] = err_file
                                rd["rbterr_checked"] = True

                            if rd["rbterr_time"] and rd["rbterr_logfile"]:
                                term_events.append({
                                    "time": rd["rbterr_time"],
                                    "gen": g,
                                    "idx": line_idx,
                                    "text": f"{rd['rbterr_time']} {puuid} RBTERR {v}/{g}/{op} "
                                            f"errored rc={rc} rank={fail_rank} "
                                            f"{rd['rbterr_logfile']}",
                                })
                                line_idx += 1

                # Print term end (if there is a PS leader step_down)
                if e != "":
                    term_events.append({
                        "time": e,
                        "gen": -1,
                        "idx": line_idx,
                        "text": f"{e} {puuid} END    term {term}\trank {r}\t{h}\tPID {p}\t{f}",
                    })
                    line_idx += 1

                for term_event in self._sort_term_events(term_events):
                    emit_report_line(term_event["time"], term_event["text"])

                if e == "":
                    # No timestamp available, keep legacy placeholder end line behavior.
                    print(" " * 18 + f"{puuid} END    term {term}\trank {r}\t{h}\tPID {p}\t{f}")

            if pool_has_time_out_of_order:
                print(f"WARN pool={puuid}: report has some out-of-order timestamps")

    def sort(self):
        """Sort the nested dictionary of pools by pool service term"""
        for puuid, pd in self._pools.items():
            tmp = dict(sorted(pd.items()))
            self._pools[puuid] = tmp
        # _pools[puuid][term]["maps"] should have been inserted in ascending order already?


def run():
    """Scan a list of daos_engine logfiles"""
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', '--output', default='/tmp/sys_logscan.txt',
                    help='Output file path (default: /tmp/sys_logscan.txt)')
    ap.add_argument('filelist', nargs='+')
    args = ap.parse_args()

    out_fname = args.output
    if os.path.exists(out_fname):
        print(f"Error: output file '{out_fname}' already exists. "
              "Rename it or specify a different path with -o.",
              file=sys.stderr)
        sys.exit(1)

    out_fd = open(out_fname, 'w')  # pylint: disable=consider-using-with
    real_stdout = sys.stdout
    sys.stdout = out_fd
    print(f'Logging to {out_fname}', file=real_stdout)

    rank_in_fname_re = re.compile(r"rank=(\d+)")

    sp = SysPools()

    for fname in args.filelist:
        if fname.endswith("cart_logtest"):
            continue

        rank = -1
        match = rank_in_fname_re.search(fname)
        if match:
            rank = int(match.group(1))

        try:
            log_iter = cart_logparse.LogIter(fname)
        except UnicodeDecodeError:
            log_iter = cart_logparse.LogIter(fname, check_encoding=True)

        if log_iter.file_corrupt:
            sys.exit(1)
        sp.scan_file(log_iter, rank=rank)

    sp.print_system()
    print(f"\n======== Pools Report ({len(sp.warnings)} warnings from scanning) ========\n")
    sp._build_rank_to_files()
    sp.sort()
    sp.print_pools()


if __name__ == '__main__':
    run()
