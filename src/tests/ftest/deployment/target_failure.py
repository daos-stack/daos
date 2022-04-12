#!/usr/bin/python
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import os
import threading

from ior_test_base import IorTestBase
from ior_utils import IorCommand
from general_utils import report_errors
from command_utils_base import CommandFailure
from job_manager_utils import get_job_manager


class TargetFailure(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify target failure is properly handled and recovered.

    :avocado: recursive
    """
    def run_ior_report_error(self, results, job_num, file_name, pool, container,
                             namespace):
        """Run IOR command and store the results to results dictionary.

        Create a new IorCommand object instead of using the one in IorTestBase because
        we'll run a test that runs multiple IOR processes at the same time.

        Args:
            results (dict): A dictionary object to store the ior metrics
            job_num (int): Assigned job number
            file_name (str): File name used for self.ior_cmd.test_file.
            oclass (str): Value for dfs_oclass and dfs_dir_oclass.
            pool (TestPool): Pool to run IOR.
            container (TestContainer): Container to run IOR.
        """
        # Update the object class depending on the test case.
        ior_cmd = IorCommand(namespace=namespace)
        ior_cmd.get_params(self)

        # Standard IOR prep sequence.
        ior_cmd.set_daos_params(self.server_group, pool, container.uuid)
        testfile = os.path.join("/", file_name)
        ior_cmd.test_file.update(testfile)

        manager = get_job_manager(
            test=self, class_name="Mpirun", job=ior_cmd, subprocess=self.subprocess,
            mpi_type="mpich")
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
        ppn = self.params.get("ppn", '/run/ior/client_processes/*')
        manager.ppn.update(ppn, 'mpirun.ppn')
        manager.processes.update(None, 'mpirun.np')

        # Run the command.
        try:
            self.log.info("--- IOR command %d start ---", job_num)
            ior_output = manager.run()
            results[job_num] = [True]
            # For debugging.
            results[job_num].extend(IorCommand.get_ior_metrics(ior_output))
            # Command worked, but append the error message if any.
            results[job_num].append(ior_output.stderr_text)
            self.log.info("--- IOR command %d end ---", job_num)
        except CommandFailure as error:
            self.log.info("--- IOR command %d failed ---", job_num)
            results[job_num] = [False, "IOR failed: {}".format(error)]

    @staticmethod
    def check_container_health(container, expected_health):
        """Check container property's Health field by calling daos container get-prop.

        Args:
            container (TestContainer): Container to call get-prop.
            expected_health (str): Expected value in the Health field.

        Returns:
            bool: True if expected_health matches the one obtained from get-prop.

        """
        output = container.get_prop()
        for cont_prop in output["response"]:
            if cont_prop["name"] == "status":
                return cont_prop["value"] == expected_health
        return False

    def verify_failure_with_protection(self, ior_namespace):
        """Jira ID: DAOS-10001.

        Verify that failing (excluding) one target from two server ranks would cause an
        error to the ongoing IOR even if data protection is used. Also verify that
        reintegrating the excluded target will bring back the system to the usable state.

        1. Run two server ranks and create a pool and a container with --properties=rf:1.
        2. Run IOR with --dfs.oclass RP_2G1/EC_2P1G1 --dfs.dir_oclass RP_2G1/EC_2P1G1
        3. While the IOR is running, exclude one target from each server rank so that IO
        fails even with replication.
        4. Verify the IOR failed.
        5. Verify that the container's Health property is UNCLEAN.
        6. Reintegrate the excluded targets and wait for the rebuild to finish.
        7. Verify that the container's Health property is HEALTHY.
        8. Restart the IOR and verify that it works. (Recovery test)
        9. Verify that a new container can be created and IOR works. (Recovery test)

        Args:
            ior_namespace (str): Yaml namespace that defines the object class used for
                IOR.
        """
        # 1. Create a pool and a container.
        self.add_pool(namespace="/run/pool_size_ratio_50/*")
        self.add_container(pool=self.pool, namespace="/run/container_with_rf/*")

        # 2. Run IOR with oclass RP_2G1 or EC_2P1G1.
        ior_results = {}
        job_num = 1
        job = threading.Thread(
            target=self.run_ior_report_error,
            args=[ior_results, job_num, "test_file_1", self.pool, self.container,
                  ior_namespace])

        job.start()

        # We need to exclude targets while IOR is running, so need to wait for a few
        # seconds for IOR to start.
        self.log.info("Waiting 5 sec for IOR to start writing data...")
        time.sleep(5)

        errors = []

        # 3. Exclude one target from two server ranks while IOR is running.
        self.pool.exclude(ranks=[1], tgt_idx="1")
        # If we exclude back to back, it would cause an error. Wait for the rebuild to
        # start before excluding the next target.
        self.pool.wait_for_rebuild(to_start=True)
        self.pool.exclude(ranks=[0], tgt_idx="1")
        self.pool.measure_rebuild_time(operation="Exclude 2 targets", interval=5)

        # Wait until the IOR thread ends.
        job.join()

        # 4. Verify that the IOR failed.
        self.log.info("----- IOR results 1 -----")
        self.log.info(ior_results)
        ior_error = ior_results[job_num][-1]
        self.log.info("IOR 1 error = %s", ior_error)
        if ior_results[job_num][0]:
            errors.append("First IOR was supposed to fail, but worked!")

        # 5. Verify that the container's Health property is UNCLEAN.
        if not self.check_container_health(
                container=self.container, expected_health="UNCLEAN"):
            errors.append("Container health isn't UNCLEAN after first IOR!")

        # 6. Reintegrate the excluded target.
        self.log.info("Reintegrate rank 1 target 1")
        # Reintegrate one target and wait for rebuild to finish before reintegrating the
        # next one.
        self.pool.reintegrate(rank="1", tgt_idx="1")
        self.pool.measure_rebuild_time(
            operation="Reintegrate rank 1 -> target 1", interval=5)
        self.log.info("Reintegrate rank 0 target 1")
        self.pool.reintegrate(rank="0", tgt_idx="1")
        self.pool.measure_rebuild_time(
            operation="Reintegrate rank 0 -> target 1", interval=5)

        # 7. Verify that the container's Health property is HEALTHY.
        if not self.check_container_health(
                container=self.container, expected_health="HEALTHY"):
            errors.append("Container health isn't HEALTHY after reintegrate!")

        # 8. Restart IOR. Should work.
        ior_results = {}
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_2", pool=self.pool,
            container=self.container, namespace=ior_namespace)

        # Verify that there's no error this time.
        self.log.info("----- IOR results 2 -----")
        self.log.info(ior_results)
        if not ior_results[job_num][0]:
            errors.append(
                "Error found in second IOR run! {}".format(ior_results[job_num][1]))

        # 9. Create a new container and run IOR.
        self.add_container(pool=self.pool, namespace="/run/container_with_rf/*")
        ior_results = {}
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_3", pool=self.pool,
            container=self.container, namespace=ior_namespace)

        # Verify that there's no error.
        self.log.info("----- IOR results 2 -----")
        self.log.info(ior_results)
        if not ior_results[job_num][0]:
            errors.append(
                "Error found in third IOR run! {}".format(ior_results[job_num][-1]))

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")

    def test_target_failure_wo_rf(self):
        """Jira ID: DAOS-10001.

        Verify that failing (excluding) one target would cause an error to the ongoing
        IOR. Also verify that reintegrating the excluded target will bring back the system
        to the usable state.

        1. Create a pool and a container.
        2. Run IOR with oclass SX.
        3. Exclude one target while IOR is running.
        4. Verify the IOR failed.
        5. Verify that the container's Health property is UNCLEAN.
        6. Reintegrate the excluded target.
        7. Verify that the container's Health property is HEALTHY.
        8. Run IOR again.
        9. Verify that there's no error this time.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,target_failure
        :avocado: tags=target_failure_wo_rf
        """
        # 1. Create a pool and a container.
        self.add_pool(namespace="/run/pool_size_ratio_50/*")
        self.add_container(pool=self.pool, namespace="/run/container_wo_rf/*")

        # 2. Run IOR with oclass SX so that excluding one target will result in a failure.
        ior_results = {}
        job_num = 1
        ior_namespace = "/run/ior_wo_rf/*"
        job = threading.Thread(
            target=self.run_ior_report_error,
            args=[ior_results, job_num, "test_file_1", self.pool, self.container,
                  ior_namespace])

        job.start()

        # Wait for a few seconds for IOR to start.
        self.log.info("Waiting 5 sec for IOR to start writing data...")
        time.sleep(5)

        errors = []

        # 3. Exclude one target while IOR is running.
        self.pool.exclude(ranks=[1], tgt_idx="0")
        self.pool.measure_rebuild_time(operation="Exclude 1 target", interval=5)

        # Wait until the IOR thread ends.
        job.join()

        # 4. Verify that the IOR failed.
        self.log.info("----- IOR results 1 -----")
        self.log.info(ior_results)
        if ior_results[job_num][0]:
            ior_error = ior_results[job_num][-1]
            errors.append(
                "First IOR was supposed to fail, but worked! {}".format(ior_error))

        # 5. Verify that the container's Health property is UNCLEAN.
        if not self.check_container_health(
                container=self.container, expected_health="UNCLEAN"):
            errors.append("Container health isn't UNCLEAN after first IOR!")

        # 6. Reintegrate the excluded target.
        self.pool.reintegrate(rank="1", tgt_idx="0")
        self.pool.measure_rebuild_time(operation="Reintegrate 1 target", interval=5)

        # 7. Verify that the container's Health property is HEALTHY.
        if not self.check_container_health(
                container=self.container, expected_health="HEALTHY"):
            errors.append("Container health isn't HEALTHY after reintegrate!")

        # 8. Run IOR again.
        ior_results = {}
        self.run_ior_report_error(
            job_num=job_num, results=ior_results, file_name="test_file_2", pool=self.pool,
            container=self.container, namespace=ior_namespace)

        # 9. Verify that there's no error this time.
        self.log.info("----- IOR results 2 -----")
        self.log.info(ior_results)
        if not ior_results[job_num][0]:
            ior_error = ior_results[job_num][1]
            errors.append("Error found in second IOR run! {}".format(ior_error))

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")

    def test_target_failure_with_rp(self):
        """Jira ID: DAOS-10001.

        Verify that failing (excluding) one target from each server rank would cause an
        error to the ongoing IOR even if object class RP_2G1 is used. Also verify that
        reintegrating the excluded target will bring back the system to the usable state.

        See verify_target_failure_with_protection for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,target_failure
        :avocado: tags=target_failure_with_rp
        """
        self.verify_failure_with_protection(ior_namespace="/run/ior_with_rp/*")

    def test_target_failure_with_ec(self):
        """Jira ID: DAOS-10001.

        Verify that failing (excluding) one target from each server rank would cause an
        error to the ongoing IOR even if object class EC_2P1G1 is used. Also verify that
        reintegrating the excluded target will bring back the system to the usable state.

        See verify_target_failure_with_protection for test steps.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,target_failure
        :avocado: tags=target_failure_with_ec
        """
        self.verify_failure_with_protection(ior_namespace="/run/ior_with_ec/*")

    def test_target_failure_parallel(self):
        """Jira ID: DAOS-10001.

        Verifying that failing a target in one pool doesn't affect other pool.

        1. Create 2 pools and a container in each pool.
        2. Run IOR with oclass SX on all containers at the same time.
        3. Exclude one target from self.pool[1] while IOR is running.
        4. Verify the IOR failed for self.pool[1].
        5. Verify the IOR worked for self.pool[0].
        6. Verify that the self.pool[1].container's Health property is UNCLEAN.
        7. Reintegrate the excluded target.
        8. Verify that the self.pool[1].container's Health property is HEALTHY.
        9. Run IOR again.
        10. Verify that there's no error this time.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=deployment,target_failure
        :avocado: tags=target_failure_parallel
        """
        self.pool = []
        self.container = []

        # 1. Create 2 pools and a container in each pool. Each pool uses about 40% of the
        # available storage.
        self.pool.append(self.get_pool(namespace="/run/pool_size_ratio_40/*"))
        self.pool.append(self.get_pool(namespace="/run/pool_size_ratio_66/*"))
        for i in range(2):
            self.container.append(
                self.get_container(pool=self.pool[i], namespace="/run/container_wo_rf/*"))

        # 2. Run IOR with oclass SX on all containers at the same time.
        ior_results = {}
        ior_namespace = "/run/ior_wo_rf/*"
        threads = []

        for pool_num in range(2):
            threads.append(
                threading.Thread(
                    target=self.run_ior_report_error,
                    args=[ior_results, pool_num, "test_file_1",
                          self.pool[pool_num], self.container[pool_num], ior_namespace]))

            threads[-1].start()

        # Wait for a few seconds for IOR to start.
        self.log.info("Waiting 5 sec for IOR to start writing data...")
        time.sleep(5)

        errors = []

        # 3. Exclude one target from self.pool[1] while IOR is running.
        excluded_pool_num = 1
        non_excluded_pool_num = 0
        self.log.info("Exclude rank 1 target 0")
        self.pool[excluded_pool_num].exclude(ranks=[1], tgt_idx="0")
        self.pool[excluded_pool_num].measure_rebuild_time(
            operation="Exclude 1 target", interval=5)

        # Wait until all the IOR threads end.
        for thread in threads:
            thread.join()

        # 4. Verify that the IOR failed for self.pool[1].
        failed_ior_result = ior_results[excluded_pool_num]
        self.log.info("----- IOR results 1 -----")
        self.log.info(failed_ior_result)
        if failed_ior_result[0]:
            msg = "First IOR {} was supposed to fail, but worked! {}".format(
                excluded_pool_num, failed_ior_result)
            errors.append(msg)

        # 5. Verify that the IOR worked for self.pool[0].
        succeeded_ior_result = ior_results[non_excluded_pool_num]
        if not succeeded_ior_result[0]:
            msg = "First IOR {} was supposed to worked, but failed! {}".format(
                non_excluded_pool_num, succeeded_ior_result)
            errors.append(msg)

        # 6. Verify that self.container[1]'s Health property is UNCLEAN.
        if not self.check_container_health(
                container=self.container[1], expected_health="UNCLEAN"):
            errors.append("Container health isn't UNCLEAN after first IOR!")

        # 7. Reintegrate the excluded target.
        self.log.info("Reintegrate target")
        self.pool[excluded_pool_num].reintegrate(rank="1", tgt_idx="0")
        self.pool[excluded_pool_num].measure_rebuild_time(
            operation="Reintegrate 1 target", interval=5)

        # 8. Verify that self.container[1]'s Health property is HEALTHY.
        if not self.check_container_health(
                container=self.container[1], expected_health="HEALTHY"):
            errors.append("Container health isn't HEALTHY after first IOR!")

        # 9. Run IOR again.
        self.run_ior_report_error(
            job_num=excluded_pool_num, results=ior_results, file_name="test_file_2",
            pool=self.pool[excluded_pool_num],
            container=self.container[excluded_pool_num], namespace=ior_namespace)

        # 10. Verify that there's no error this time.
        self.log.info("----- IOR results 2 -----")
        ior_result = ior_results[excluded_pool_num]
        self.log.debug(ior_result)
        if not ior_result[0]:
            errors.append("Error found in second IOR run! {}".format(ior_result[1]))

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")
