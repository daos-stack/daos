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
        metric_to_data = {}
        for metric in self.telemetry.ENGINE_EVENT_METRICS:
            metric_to_data[metric] = self.telemetry.get_metrics(name=metric)
        self.log.info("metric_to_data = %s", metric_to_data)

        # Omit "engine" from the variable name for brevity. The indices correspond to ranks.
        events_dead_ranks = [None for _ in range(rank_count)]
        hosts = list(self.hostlist_servers)
        for host in hosts:
            engine_events_dead_ranks = metric_to_data["engine_events_dead_ranks"]
            metrics = engine_events_dead_ranks[host]["engine_events_dead_ranks"]["metrics"]
            for metric in metrics:
                rank = int(metric["labels"]["rank"])
                events_dead_ranks[rank] = metric["value"]

        events_last_event_ts = [None for _ in range(rank_count)]
        for host in hosts:
            engine_events_last_event_ts = metric_to_data["engine_events_last_event_ts"]
            metrics = engine_events_last_event_ts[host]["engine_events_last_event_ts"]["metrics"]
            for metric in metrics:
                rank = int(metric["labels"]["rank"])
                events_last_event_ts[rank] = metric["value"]

        servicing_at = [None for _ in range(rank_count)]
        for host in hosts:
            metrics = metric_to_data["engine_servicing_at"][host]["engine_servicing_at"]["metrics"]
            for metric in metrics:
                rank = int(metric["labels"]["rank"])
                servicing_at[rank] = metric["value"]

        started_at = [None for _ in range(rank_count)]
        for host in hosts:
            metrics = metric_to_data["engine_started_at"][host]["engine_started_at"]["metrics"]
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
        rank_stopped = False
        for count in range(3):
            time.sleep(5)
            query_out = dmg_command.system_query()
            for member in query_out["response"]["members"]:
                if member["rank"] == restart_rank:
                    if member["state"] == "stopped" or member["state"] == "excluded":
                        rank_stopped = True
                        break
            if rank_stopped:
                self.log.info("Rank %d is stopped. count = %d", restart_rank, count)
                break
            self.log.info("Rank %d is not stopped. Check again. count = %d", restart_rank, count)
        if not rank_stopped:
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
        :avocado: tags=test_engine_events
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

        # engine_events_dead_ranks requirements:
        # 1. Restarted rank value shouldn't change before and after.
        # 2. For the non-restarted-ranks, at least one after value should be higher than before.
        # 3. For the non-restarted-ranks, after value shouldn't go down.
        if events_dead_ranks_0[restart_rank] != events_dead_ranks_1[restart_rank]:
            msg = (f"engine_events_dead_ranks value for restarted rank {restart_rank} "
                   f"changed before and after! Before = {events_dead_ranks_0[restart_rank]}; "
                   f"After = {events_dead_ranks_1[restart_rank]}")
            errors.append(msg)

        increase_found = False
        for rank in range(rank_count):
            if rank != restart_rank:
                if events_dead_ranks_0[rank] < events_dead_ranks_1[rank]:
                    increase_found = True
                elif events_dead_ranks_0[rank] > events_dead_ranks_1[rank]:
                    msg = (f"engine_events_dead_ranks value for rank {rank} went down! "
                           f"Before = {events_dead_ranks_0[rank]}; "
                           f"After = {events_dead_ranks_1[rank]}")
                    errors.append(msg)
        if not increase_found:
            msg = (f"No value increase detected for engine_events_dead_ranks! "
                   f"Before = {events_dead_ranks_0}; After = {events_dead_ranks_1}")
            errors.append(msg)

        # engine_events_last_event_ts requirements:
        # 1. Restarted rank value shouldn't change before and after.
        # 2. All after values except for restarted rank should go up.
        for rank in range(rank_count):
            if rank == restart_rank:
                if events_last_event_ts_0[rank] != events_last_event_ts_1[rank]:
                    msg = (f"engine_events_last_events_ts value for restarted rank {rank} "
                           f"changed! Before = {events_last_event_ts_0[rank]}; "
                           f"After = {events_last_event_ts_1[rank]}")
                    errors.append(msg)
            else:
                if events_last_event_ts_0[rank] >= events_last_event_ts_1[rank]:
                    msg = (f"No increase detected in engine_events_last_events_ts for rank {rank}! "
                           f"Before = {events_last_event_ts_0[rank]}; "
                           f"After = {events_last_event_ts_1[rank]}")
                    errors.append(msg)

        # engine_servicing_at requirements:
        # 1. Restarted rank value should increase.
        # 2. All after values except for restarted rank should remain the same.
        for rank in range(rank_count):
            if rank == restart_rank:
                if servicing_at_0[rank] >= servicing_at_1[rank]:
                    msg = (f"engine_servicing_at value for restarted rank {rank} "
                           f"didn't increase! Before = {servicing_at_0[rank]}; "
                           f"After = {servicing_at_1[rank]}")
                    errors.append(msg)
            else:
                if servicing_at_0[rank] != servicing_at_1[rank]:
                    msg = (f"engine_servicing_at for rank {rank} changed! "
                           f"Before = {servicing_at_0[rank]}; "
                           f"After = {servicing_at_1[rank]}")
                    errors.append(msg)

        # engine_started_at requirements:
        # 1. Restarted rank value should increase.
        # 2. All after values except for restarted rank should remain the same.
        for rank in range(rank_count):
            if rank == restart_rank:
                if started_at_0[rank] >= started_at_1[rank]:
                    msg = (f"engine_started_at value for restarted rank {rank} "
                           f"didn't increase! Before = {started_at_0[rank]}; "
                           f"After = {started_at_1[rank]}")
                    errors.append(msg)
            else:
                if started_at_0[rank] != started_at_1[rank]:
                    msg = (f"engine_started_at for rank {rank} changed! "
                           f"Before = {started_at_0[rank]}; "
                           f"After = {started_at_1[rank]}")
                    errors.append(msg)

        self.log.info("######## Errors ########")
        report_errors(test=self, errors=errors)
