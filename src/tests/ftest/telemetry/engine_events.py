'''
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time

from dmg_utils import check_system_query_status
from general_utils import report_errors
from telemetry_test_base import TestWithTelemetry


class EngineEvents(TestWithTelemetry):
    """
    Verify engine-related event values in telemetry.

    :avocado: recursive
    """

    def collect_telemetry(self, rank_count):
        """Collect the following engine event values.

        engine_events_dead_ranks, engine_events_last_event_ts, engine_servicing_at, and
        engine_servicing_at.

        Args:
            rank_count (int): Number of ranks in the system.

        Returns:
            list: Four lists. Each list contains the above telemetry value for each rank.

        """
        metric_to_data = self.telemetry.get_metrics(
            name=','.join(self.telemetry.ENGINE_EVENT_METRICS))
        self.log.info("metric_to_data = %s", metric_to_data)

        # Omit "engine" from the variable name for brevity. The indices correspond to ranks.
        events_dead_ranks = [None for _ in range(rank_count)]
        hosts = list(self.hostlist_servers)
        for host in hosts:
            metrics = metric_to_data[host]["engine_events_dead_ranks"]["metrics"]
            for metric in metrics:
                rank = int(metric["labels"]["rank"])
                events_dead_ranks[rank] = metric["value"]

        events_last_event_ts = [None for _ in range(rank_count)]
        for host in hosts:
            metrics = metric_to_data[host]["engine_events_last_event_ts"]["metrics"]
            for metric in metrics:
                rank = int(metric["labels"]["rank"])
                events_last_event_ts[rank] = metric["value"]

        servicing_at = [None for _ in range(rank_count)]
        for host in hosts:
            metrics = metric_to_data[host]["engine_servicing_at"]["metrics"]
            for metric in metrics:
                rank = int(metric["labels"]["rank"])
                servicing_at[rank] = metric["value"]

        started_at = [None for _ in range(rank_count)]
        for host in hosts:
            metrics = metric_to_data[host]["engine_started_at"]["metrics"]
            for metric in metrics:
                rank = int(metric["labels"]["rank"])
                started_at[rank] = metric["value"]

        self.log.info("events_dead_ranks = %s", events_dead_ranks)
        self.log.info("events_last_event_ts =%s", events_last_event_ts)
        self.log.info("servicing_at = %s", servicing_at)
        self.log.info("started_at = %s", started_at)

        return (events_dead_ranks, events_last_event_ts, servicing_at, started_at)

    def check_rank_stopped(self, restart_rank, dmg_command):
        """Check the restart_rank is stopped.

        If it hasn't been stopped, fail the test. This method is created to avoid the pylint error.

        Args:
            restart_rank (int): Restarted rank.
            dmg_command (DmgCommand): DmgCommand object.
        """
        for count in range(3):
            time.sleep(5)
            query_out = dmg_command.system_query()
            for member in query_out["response"]["members"]:
                if member["rank"] == restart_rank:
                    if member["state"] == "stopped" or member["state"] == "excluded":
                        self.log.info("Rank %d is stopped. count = %d", restart_rank, count)
                        return
            self.log.info("Rank %d is not stopped. Check again. count = %d", restart_rank, count)
        self.fail(f"Rank {restart_rank} didn't stop!")

    def check_rank_restarted(self, restart_rank, dmg_command):
        """Check the restart_rank is restarted.

        If it hasn't been restarted, fail the test. This method is created to avoid the pylint
        error.

        Args:
            restart_rank (int): Restarted rank.
            dmg_command (DmgCommand): DmgCommand object.
        """
        rank_restarted = False
        for count in range(3):
            time.sleep(5)
            query_out = dmg_command.system_query()
            for member in query_out["response"]["members"]:
                if member["rank"] == restart_rank:
                    if member["state"] == "joined":
                        rank_restarted = True
                        break
            if rank_restarted:
                self.log.info("Rank %d is joined. count = %d", restart_rank, count)
                break
            self.log.info("Rank %d is not joined. Check again. count = %d", restart_rank, count)
        if not rank_restarted:
            self.fail(f"Rank {restart_rank} didn't restart!")

    def test_engine_events(self):
        """Test engine-related events after an engine is restarted.

        1. Check that all ranks have started.
        2. Gather initial data for the required metrics for all the server nodes.
        3. Stop a rank.
        4. Verify the desired rank has stopped successfully.
        5. Restart the stopped rank after several seconds.
        6. Verify the desired rank restarted successfully.
        7. Gather final data for required metrics with the same command as in step 2.
        8. Compare the before and after values.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=EngineEvents,test_engine_events
        """
        # 1. Check that all ranks have started.
        self.log_step("Check that all ranks have started.")
        dmg_command = self.get_dmg_command()
        if not check_system_query_status(data=dmg_command.system_query()):
            self.fail("Engine failure detected at the beginning of the test!")

        # 2. Gather initial data for the required metrics for all the server nodes.
        self.log_step("Gather initial data for the required metrics for all the server nodes.")
        rank_count = self.server_managers[0].engines
        telemetry_before = self.collect_telemetry(rank_count=rank_count)

        # 3. Stop a rank.
        restart_rank = rank_count - 1
        self.log_step(f"Stop rank {restart_rank} and wait for a while for RPCs to circulate")
        dmg_command.system_stop(ranks=str(restart_rank))
        # Wait for the RPCs to go around the ranks.
        time.sleep(40)

        # 4. Verify the desired rank has stopped successfully.
        self.log_step("Verify the desired rank has stopped successfully.")
        self.check_rank_stopped(restart_rank=restart_rank, dmg_command=dmg_command)

        # 5. Restart the stopped rank after several seconds.
        self.log_step("Restart the stopped rank after a few seconds.")
        dmg_command.system_start(ranks=str(restart_rank))

        # 6. Verify the desired rank restarted successfully.
        self.log_step("Verify the desired rank restarted successfully.")
        self.check_rank_restarted(restart_rank=restart_rank, dmg_command=dmg_command)

        # 7. Gather final data for required metrics with the same command as in step 2.
        self.log_step("Gather final data for required metrics with the same command as in step 2.")
        telemetry_after = self.collect_telemetry(rank_count=rank_count)

        # 8. Compare the before and after values.
        self.log_step("Compare the before and after values.")
        # Obtain the value list for each telemetry. Use 0 for before and 1 for after for the
        # variable name for brevity.
        events_dead_ranks_0 = telemetry_before[0]
        events_last_event_ts_0 = telemetry_before[1]
        servicing_at_0 = telemetry_before[2]
        started_at_0 = telemetry_before[3]
        events_dead_ranks_1 = telemetry_after[0]
        events_last_event_ts_1 = telemetry_after[1]
        servicing_at_1 = telemetry_after[2]
        started_at_1 = telemetry_after[3]
        errors = []
        events_dead_ranks_results = ["engine_events_dead_ranks Results", "--------------------"]
        events_last_event_ts_results = ["engine_events_last_event_ts Results", "------------------"]
        servicing_at_results = ["engine_servicing_at Results", "--------------------"]
        started_at_results = ["engine_started_at Results", "--------------------"]

        # engine_events_dead_ranks requirements:
        # 1. Restarted rank value shouldn't change before and after.
        # 2. For the non-restarted-ranks, at least one after value should be higher than before.
        # 3. For the non-restarted-ranks, after value shouldn't go down.
        result = ""
        if events_dead_ranks_0[restart_rank] != events_dead_ranks_1[restart_rank]:
            msg = (f"engine_events_dead_ranks value for restarted rank {restart_rank} "
                   f"changed before and after! Before = {events_dead_ranks_0[restart_rank]}; "
                   f"After = {events_dead_ranks_1[restart_rank]}")
            errors.append(msg)
            result = (f"Rank {restart_rank}: {events_dead_ranks_0[restart_rank]} -> "
                      f"{events_dead_ranks_1[restart_rank]} Fail (Value changed)")
        else:
            result = (f"Rank {restart_rank}: {events_dead_ranks_0[restart_rank]} -> "
                      f"{events_dead_ranks_1[restart_rank]} Pass (Value unchanged)")
        events_dead_ranks_results.append(result)

        increase_found = False
        for rank in range(rank_count):
            if rank != restart_rank:
                result = ""
                if events_dead_ranks_0[rank] < events_dead_ranks_1[rank]:
                    increase_found = True
                    result = (f"Rank {rank}: {events_dead_ranks_0[rank]} -> "
                              f"{events_dead_ranks_1[rank]} Pass (Value increased)")
                elif events_dead_ranks_0[rank] > events_dead_ranks_1[rank]:
                    msg = (f"engine_events_dead_ranks value for rank {rank} went down! "
                           f"Before = {events_dead_ranks_0[rank]}; "
                           f"After = {events_dead_ranks_1[rank]}")
                    errors.append(msg)
                    result = (f"Rank {rank}: {events_dead_ranks_0[rank]} -> "
                              f"{events_dead_ranks_1[rank]} Fail (Value decreased)")
                else:
                    result = (f"Rank {rank}: {events_dead_ranks_0[rank]} -> "
                              f"{events_dead_ranks_1[rank]} Pass (Value unchanged)")
                events_dead_ranks_results.append(result)
        if not increase_found:
            msg = (f"No value increase detected for engine_events_dead_ranks! "
                   f"Before = {events_dead_ranks_0}; After = {events_dead_ranks_1}")
            errors.append(msg)

        # engine_events_last_event_ts requirements:
        # 1. Restarted rank value shouldn't change before and after.
        # 2. All after values except for restarted rank should go up.
        for rank in range(rank_count):
            result = ""
            if rank == restart_rank:
                if events_last_event_ts_0[rank] != events_last_event_ts_1[rank]:
                    msg = (f"engine_events_last_events_ts value for restarted rank {rank} "
                           f"changed! Before = {events_last_event_ts_0[rank]}; "
                           f"After = {events_last_event_ts_1[rank]}")
                    errors.append(msg)
                    result = (f"Rank {rank}: {events_last_event_ts_0[rank]} -> "
                              f"{events_last_event_ts_1[rank]} Fail (Value changed)")
                else:
                    result = (f"Rank {rank}: {events_last_event_ts_0[rank]} -> "
                              f"{events_last_event_ts_1[rank]} Pass (Value unchanged)")
            else:
                if events_last_event_ts_0[rank] >= events_last_event_ts_1[rank]:
                    msg = (f"No increase detected in engine_events_last_events_ts for rank {rank}! "
                           f"Before = {events_last_event_ts_0[rank]}; "
                           f"After = {events_last_event_ts_1[rank]}")
                    errors.append(msg)
                    result = (f"Rank {rank}: {events_last_event_ts_0[rank]} -> "
                              f"{events_last_event_ts_1[rank]} Fail (Value decreased/unchanged)")
                else:
                    result = (f"Rank {rank}: {events_last_event_ts_0[rank]} -> "
                              f"{events_last_event_ts_1[rank]} Pass (Value increased)")
            events_last_event_ts_results.append(result)

        # engine_servicing_at requirements:
        # 1. Restarted rank value should increase.
        # 2. All after values except for restarted rank should remain the same.
        for rank in range(rank_count):
            result = ""
            if rank == restart_rank:
                if servicing_at_0[rank] >= servicing_at_1[rank]:
                    msg = (f"engine_servicing_at value for restarted rank {rank} "
                           f"didn't increase! Before = {servicing_at_0[rank]}; "
                           f"After = {servicing_at_1[rank]}")
                    errors.append(msg)
                    result = (f"Rank {rank}: {servicing_at_0[rank]} -> "
                              f"{servicing_at_1[rank]} Fail (Value decreased/unchanged)")
                else:
                    result = (f"Rank {rank}: {servicing_at_0[rank]} -> "
                              f"{servicing_at_1[rank]} Pass (Value increased)")
            else:
                if servicing_at_0[rank] != servicing_at_1[rank]:
                    msg = (f"engine_servicing_at for rank {rank} changed! "
                           f"Before = {servicing_at_0[rank]}; "
                           f"After = {servicing_at_1[rank]}")
                    errors.append(msg)
                    result = (f"Rank {rank}: {servicing_at_0[rank]} -> "
                              f"{servicing_at_1[rank]} Fail (Value changed)")
                else:
                    result = (f"Rank {rank}: {servicing_at_0[rank]} -> "
                              f"{servicing_at_1[rank]} Pass (Value unchanged)")
            servicing_at_results.append(result)

        # engine_started_at requirements:
        # 1. Restarted rank value should increase.
        # 2. All after values except for restarted rank should remain the same.
        for rank in range(rank_count):
            result = ""
            if rank == restart_rank:
                if started_at_0[rank] >= started_at_1[rank]:
                    msg = (f"engine_started_at value for restarted rank {rank} "
                           f"didn't increase! Before = {started_at_0[rank]}; "
                           f"After = {started_at_1[rank]}")
                    errors.append(msg)
                    result = (f"Rank {rank}: {started_at_0[rank]} -> "
                              f"{started_at_1[rank]} Fail (Value decreased/unchanged)")
                else:
                    result = (f"Rank {rank}: {started_at_0[rank]} -> "
                              f"{started_at_1[rank]} Pass (Value increased)")
            else:
                if started_at_0[rank] != started_at_1[rank]:
                    msg = (f"engine_started_at for rank {rank} changed! "
                           f"Before = {started_at_0[rank]}; "
                           f"After = {started_at_1[rank]}")
                    errors.append(msg)
                    result = (f"Rank {rank}: {started_at_0[rank]} -> "
                              f"{started_at_1[rank]} Fail (Value changed)")
                else:
                    result = (f"Rank {rank}: {started_at_0[rank]} -> "
                              f"{started_at_1[rank]} Pass (Value unchanged)")
            started_at_results.append(result)

        self.log.info("######## Test Summary ########")
        for line in events_dead_ranks_results:
            self.log.info(line)
        self.log.info("")
        for line in events_last_event_ts_results:
            self.log.info(line)
        self.log.info("")
        for line in servicing_at_results:
            self.log.info(line)
        self.log.info("")
        for line in started_at_results:
            self.log.info(line)
        self.log.info("")
        report_errors(test=self, errors=errors)
