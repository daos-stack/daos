'''
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time

from general_utils import report_errors
from telemetry_test_base import TestWithTelemetry


class EngineEvents(TestWithTelemetry):
    """Verify engine-related event values in telemetry.

    :avocado: recursive
    """

    def collect_events_dead_ranks(self, metric_to_data, rank_count):
        """Collect engine_events_dead_ranks values from given metric data.

        Args:
            metric_to_data (dict): Telemetry output that stores engine_events_dead_ranks for each
                host.
            rank_count (int): Total number of ranks in the system.

        Returns:
            list: engine_events_dead_ranks value for each rank.

        """
        events_dead_ranks = [None for _ in range(rank_count)]
        for host in self.hostlist_servers:
            metrics = metric_to_data[host]["engine_events_dead_ranks"]["metrics"]
            for metric in metrics:
                rank = int(metric["labels"]["rank"])
                events_dead_ranks[rank] = metric["value"]

        return events_dead_ranks

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
        events_dead_ranks = self.collect_events_dead_ranks(
            metric_to_data=metric_to_data, rank_count=rank_count)

        hosts = list(self.hostlist_servers)
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

    def verify_events_last_events_ts(self, rank_count, restart_rank, events_last_event_ts_0,
                                     events_last_event_ts_1, errors, events_last_event_ts_results):
        """Verify engine_events_last_events_ts values.

        Requirements:
        1. Restarted rank value shouldn't change before and after.
        2. All after values except for restarted rank should go up.

        Args:
            rank_count (int): Rank count.
            restart_rank (int): Restarted rank.
            events_last_event_ts_0 (dict): engine_events_last_event_ts values before.
            events_last_event_ts_1 (dict): engine_events_last_event_ts values after.
            errors (list): Errors.
            events_last_event_ts_results (list): Dictionary to store the results, which are printed
                at the end of the test.
        """
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

    def verify_servicing_at(self, rank_count, restart_rank, servicing_at_0, servicing_at_1, errors,
                            servicing_at_results):
        """Verify engine_servicing_at values.

        Requirements:
        1. Restarted rank value should increase.
        2. All after values except for restarted rank should remain the same.

        Args:
            rank_count (int): Rank count.
            restart_rank (int): Restarted rank.
            servicing_at_0 (dict): engine_servicing_at values before.
            servicing_at_1 (dict): engine_servicing_at values after.
            errors (list): Errors.
            servicing_at_results (list): Dictionary to store the results, which are printed at the
                end of the test.
        """
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

    def verify_started_at(self, rank_count, restart_rank, started_at_0, started_at_1, errors,
                          started_at_results):
        """Verify engine_started_at values.

        Requirements:
        1. Restarted rank value should increase.
        2. All after values except for restarted rank should remain the same.

        Args:
            rank_count (int): Rank count.
            restart_rank (int): Restarted rank.
            started_at_0 (dict): engine_started_at values before.
            started_at_1 (dict): engine_started_at values after.
            errors (list): Errors.
            started_at_results (list): Dictionary to store the results, which are printed at the
                end of the test.
        """
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

    def test_engine_events(self):
        """Test engine-related events after an engine is restarted.

        Steps are described in DAOS-15181.

        1. Gather initial data for the required metrics for all the server nodes.
        2. Stop a rank.
        3. Verify the desired rank has stopped successfully.
        4. Verify that the RPCs circulated.
        5. Restart the stopped rank.
        6. Verify the desired rank restarted successfully.
        7. Gather final data for required metrics with the same command as in step 1.
        8. Compare the before and after values.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=EngineEvents,test_engine_events
        """
        # 1. Gather initial data for the required metrics for all the server nodes.
        self.log_step("Gather initial data for the required metrics for all the server nodes.")
        rank_count = self.server_managers[0].engines
        telemetry_before = self.collect_telemetry(rank_count=rank_count)

        # 2. Stop a rank.
        restart_rank = rank_count - 1
        self.log_step(f"Stop rank {restart_rank} and wait for a while for RPCs to circulate")
        self.server_managers[0].stop_ranks(ranks=[restart_rank])

        # 3. Verify the desired rank has stopped successfully.
        self.log_step("Verify the desired rank has stopped successfully.")
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[restart_rank], valid_states=["stopped", "excluded"], max_checks=15)
        if failed_ranks:
            self.fail(f"Rank {restart_rank} didn't stop!")

        # 4. Verify that the RPCs circulated.
        for count in range(10):
            time.sleep(5)
            metric_to_data = self.telemetry.get_metrics(name="engine_events_dead_ranks")
            self.log.info("metric_to_data = %s", metric_to_data)

            # Omit "engine" from the variable name for brevity. The indices correspond to ranks.
            events_dead_ranks = self.collect_events_dead_ranks(
                metric_to_data=metric_to_data, rank_count=rank_count)

            # Among the joined ranks, if at least one of them has 1, we conclude that it's
            # circulated.
            if any(rank == 1 for rank in events_dead_ranks):
                self.log.info("RPCs cirulated.")
                break

            self.log.info("RPCs didn't circulate. Check again. %d", count)

        # 5. Restart the stopped rank.
        self.log_step("Restart the stopped rank.")
        self.server_managers[0].start_ranks(ranks=[restart_rank])

        # 6. Verify the desired rank restarted successfully.
        self.log_step("Verify the desired rank restarted successfully.")
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[restart_rank], valid_states=["joined"], max_checks=15)
        if failed_ranks:
            self.fail(f"Rank {restart_rank} didn't start!")

        # 7. Gather final data for required metrics with the same command as in step 1.
        self.log_step("Gather final data for required metrics with the same command as in step 1.")
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
        self.verify_events_last_events_ts(
            rank_count=rank_count, restart_rank=restart_rank,
            events_last_event_ts_0=events_last_event_ts_0,
            events_last_event_ts_1=events_last_event_ts_1, errors=errors,
            events_last_event_ts_results=events_last_event_ts_results)

        # engine_servicing_at requirements:
        # 1. Restarted rank value should increase.
        # 2. All after values except for restarted rank should remain the same.
        self.verify_servicing_at(
            rank_count=rank_count, restart_rank=restart_rank, servicing_at_0=servicing_at_0,
            servicing_at_1=servicing_at_1, errors=errors, servicing_at_results=servicing_at_results)

        # engine_started_at requirements:
        # 1. Restarted rank value should increase.
        # 2. All after values except for restarted rank should remain the same.
        self.verify_started_at(
            rank_count=rank_count, restart_rank=restart_rank, started_at_0=started_at_0,
            started_at_1=started_at_1, errors=errors, started_at_results=started_at_results)

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
