'''
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import ctypes

from general_utils import create_string_buffer
from pydaos.raw import DaosObjClass, IORequest
from telemetry_test_base import TestWithTelemetry
from test_utils_container import add_container
from test_utils_pool import add_pool


class DkeyAkeyEnumPunch(TestWithTelemetry):
    """
    Test Class Description:
    Verify enum and punch values for dkey and akey.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DkeyAkeyEnumPunch object."""
        super().__init__(*args, **kwargs)
        self.ioreqs = []
        self.dkeys_a = []
        self.dkeys_b = []
        self.dkey_strs_a = []
        self.dkey_strs_b = []
        self.akey_strs = []

    def set_num_targets(self, pool):
        """Define total number of targets and targets per rank from pool query.

        Sometimes the total number of targets and targets per rank are different
        from what we specify in the server config.

        Args:
            pool (TestPool): pool from which to query the target information

        Returns:
            tuple: total number of targets, number of targets per server host
        """
        pool.set_query_data()
        total_targets = pool.query_data["response"]["total_targets"]

        # Calculate targets per rank from the total targets.
        server_count = len(self.server_managers[0].hosts)
        targets_per_rank = int(total_targets / server_count)
        return total_targets, targets_per_rank

    def write_objects_insert_keys(self, container, obj_type, obj_count):
        """Write objects and insert dkeys and akeys in them.

        Args:
            container (TestContainer): Container.
            obj_type (str): Object class.
            obj_count (int): number of objects to write
        """
        for idx in range(obj_count):
            self.ioreqs.append(IORequest(
                context=self.context, container=container.container, obj=None,
                objtype=obj_type))

            # Prepare 2 dkeys and 1 akey in each dkey. Use the same akey for
            # both dkeys.
            dkey_str_a = b"Sample dkey A %d" % idx
            dkey_str_b = b"Sample dkey B %d" % idx
            akey_str = b"Sample akey %d" % idx
            data_str = b"Sample data %d" % idx
            self.dkey_strs_a.append(dkey_str_a)
            self.dkey_strs_b.append(dkey_str_b)
            self.akey_strs.append(akey_str)
            data = create_string_buffer(data_str)

            # Pass in length of the key so that it won't have \0 termination.
            # Not necessary here because we're not interested in the list
            # output. Just for debugging.
            self.dkeys_a.append(
                create_string_buffer(value=dkey_str_a, size=len(dkey_str_a)))
            self.dkeys_b.append(
                create_string_buffer(value=dkey_str_b, size=len(dkey_str_b)))
            akey = create_string_buffer(value=akey_str, size=len(akey_str))
            c_size = ctypes.c_size_t(ctypes.sizeof(data))

            # Insert the dkeys.
            self.ioreqs[-1].single_insert(
                dkey=self.dkeys_a[-1], akey=akey, value=data, size=c_size)
            self.ioreqs[-1].single_insert(
                dkey=self.dkeys_b[-1], akey=akey, value=data, size=c_size)

    def punch_all_keys(self, obj_count):
        """Punch all dkeys and akeys in the objects.

        Args:
            obj_count (int): number of objects
        """
        for idx in range(obj_count):
            self.ioreqs[idx].obj.punch_akeys(
                0, dkey=self.dkey_strs_a[idx], akeys=[self.akey_strs[idx]])
            self.ioreqs[idx].obj.punch_akeys(
                0, dkey=self.dkey_strs_b[idx], akeys=[self.akey_strs[idx]])
            # Punch dkeys one at a time. DAOS-8945
            self.ioreqs[idx].obj.punch_dkeys(0, dkeys=[self.dkey_strs_a[idx]])
            self.ioreqs[idx].obj.punch_dkeys(0, dkeys=[self.dkey_strs_b[idx]])
            if idx % 10 == 0:
                self.log.info("Keys punched %d", idx)

    def verify_active_latency(self, prefix, test_latency, total_targets, targets_per_rank):
        """Call the dmg telemetry command with given prefix and verify.

        Obtain and verify the io metrics 1 to 4 in test_dkey_akey_enum_punch()

        Args:
            prefix (str): Metrics prefix for the metric that has min, max,
                mean, or stddev at the end.
            test_latency (bool): Whether to verify the latency metric.
            total_targets (int): total number of targets
            targets_per_rank (int): number of targets per rank

        Returns:
            list: a list of errors
        """
        metrics = self.get_min_max_mean_stddev(prefix, total_targets, targets_per_rank)
        return self.verify_stats(metrics, prefix, test_latency)

    def report_status(self, errors):
        """Report any detected errors.

        Args:
            errors (list): list of errors
        """
        if errors:
            self.log.error("Detected errors:")
            first_error = None
            for error in errors:
                if not first_error:
                    first_error = error
                self.log.error("  - %s", error)
            self.fail(first_error)
        self.log.info("Test passed")

    def test_dkey_akey_enum_punch(self):
        """Test count and active for enum and punch.

        Test Steps:
        1. Write 100 objects.
        2. Insert 2 dkeys per object. Insert 1 akey per dkey. Use OC_S1.
        3. Punch all akeys and dkeys.
        4. Call list_dkey() and list_akey() on the objects.
        5. Verify the metrics below.

        --- Metrics tested ---
        1. engine_pool_ops_dkey_enum
        Number of list_dkey() calls made to each object. Since we create 100
        objects and call this on all of them, we expect this value to sum up to
        100.

        2. engine_pool_ops_akey_enum
        Number of list_akey() calls made to each dkey. We create 2 dkeys per
        object and there are 100 objects, so we expect this value to sum up to
        200.

        3. engine_pool_ops_dkey_punch
        Number of dkeys punched. There are 200 dkeys total and we punch them one
        at a time, so we expect this value to sum up to 200.

        4. engine_pool_ops_akey_punch
        Number of akeys punched. There are 200 akeys total and we punch them one
        at a time, so we expect this value to sum up to 200.

        5. dkey enum active and latency
        engine_io_ops_dkey_enum_active_max
        engine_io_ops_dkey_enum_active_mean
        engine_io_ops_dkey_enum_active_min
        engine_io_ops_dkey_enum_active_stddev
        engine_io_ops_dkey_enum_latency_max
        engine_io_ops_dkey_enum_latency_mean
        engine_io_ops_dkey_enum_latency_min
        engine_io_ops_dkey_enum_latency_stddev

        Active means the number of list_dkey() called at the same time. e.g.,
        If 2 calls are made simultaneously and that's the highest, we would see
        engine_io_ops_dkey_enum_active_max = 2. However, in this test, we expect
        the max to be always 1 because making it more than 1 isn't
        straightforward.

        Latency is the time it took to process the list_dkey() calls. The slight
        difference from active is that min would contain the actual latency and
        not 0 when there's at least one call.

        6. akey enum active and latency
        engine_io_ops_akey_enum_active_max
        engine_io_ops_akey_enum_active_mean
        engine_io_ops_akey_enum_active_min
        engine_io_ops_akey_enum_active_stddev
        engine_io_ops_akey_enum_latency_max
        engine_io_ops_akey_enum_latency_mean
        engine_io_ops_akey_enum_latency_min
        engine_io_ops_akey_enum_latency_stddev

        Same idea as dkey. We also expect to see very similar output as in dkey.

        7. dkey punch active and latency
        engine_io_ops_dkey_punch_active_max
        engine_io_ops_dkey_punch_active_mean
        engine_io_ops_dkey_punch_active_min
        engine_io_ops_dkey_punch_active_stddev
        engine_io_ops_dkey_punch_latency_max
        engine_io_ops_dkey_punch_latency_mean
        engine_io_ops_dkey_punch_latency_min
        engine_io_ops_dkey_punch_latency_stddev

        Same as 5 except we're measuring the active and latency for
        punch_dkeys() calls.

        8. akey punch active and latency
        engine_io_ops_akey_punch_active_max
        engine_io_ops_akey_punch_active_mean
        engine_io_ops_akey_punch_active_min
        engine_io_ops_akey_punch_active_stddev
        engine_io_ops_akey_punch_latency_max
        engine_io_ops_akey_punch_latency_mean
        engine_io_ops_akey_punch_latency_min
        engine_io_ops_akey_punch_latency_stddev

        Same idea as dkey. We also expect to see very similar output as in dkey.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=telemetry
        :avocado: tags=DkeyAkeyEnumPunch,test_dkey_akey_enum_punch
        """
        errors = []
        pool = add_pool(self)
        container = add_container(self, pool)
        container.open()
        total_targets, targets_per_rank = self.set_num_targets(pool)
        obj_count = 100

        # Object type needs to be OC_S1 so that the objects are spread across all targets.
        self.write_objects_insert_keys(container, DaosObjClass.OC_S1, obj_count)

        # Call list_dkey() and list_akey() on each object.
        for idx in range(obj_count):
            _ = self.ioreqs[idx].list_dkey()
            _ = self.ioreqs[idx].list_akey(dkey=self.dkeys_a[idx])
            _ = self.ioreqs[idx].list_akey(dkey=self.dkeys_b[idx])

        self.punch_all_keys(obj_count)

        self.telemetry.dmg.verbose = False

        # Obtain and verify the io metrics 1 to 4. ###
        # engine_pool_ops_dkey_enum
        pool_dkey_enum = self.telemetry.ENGINE_POOL_OPS_DKEY_ENUM_METRICS
        # engine_pool_ops_akey_enum
        pool_akey_enum = self.telemetry.ENGINE_POOL_OPS_AKEY_ENUM_METRICS
        # engine_pool_ops_dkey_punch
        pool_dkey_punch = self.telemetry.ENGINE_POOL_OPS_DKEY_PUNCH_METRICS
        # engine_pool_ops_akey_punch
        pool_akey_punch = self.telemetry.ENGINE_POOL_OPS_AKEY_PUNCH_METRICS
        specific_metrics = [
            pool_dkey_enum, pool_akey_enum,
            pool_dkey_punch, pool_akey_punch,
        ]
        pool_out = self.telemetry.get_pool_metrics(specific_metrics)

        # Verify dkey_enum total is 100.
        dkey_enum_total = self.sum_values(metric_out=pool_out[pool_dkey_enum])
        if dkey_enum_total != 100:
            msg = "dkey enum total is not 100! Actual = {}".format(
                dkey_enum_total)
            errors.append(msg)

        # Verify akey_enum total is 200.
        akey_enum_total = self.sum_values(metric_out=pool_out[pool_akey_enum])
        if akey_enum_total != 200:
            msg = "akey enum total is not 200! Actual = {}".format(
                akey_enum_total)
            errors.append(msg)

        # Verify dkey_punch total is 200.
        dkey_punch_total = self.sum_values(metric_out=pool_out[pool_dkey_punch])
        if dkey_punch_total != 200:
            msg = "dkey punch total is not 200! Actual = {}".format(
                dkey_punch_total)
            errors.append(msg)

        # Verify akey_punch total is 200.
        akey_punch_total = self.sum_values(metric_out=pool_out[pool_akey_punch])
        if akey_punch_total != 200:
            msg = "akey punch total is not 200! Actual = {}".format(
                akey_punch_total)
            errors.append(msg)

        # Verify active and latency; metrics 5 to 8. ###
        # Verify dkey enum active.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_dkey_enum_active_", False, total_targets, targets_per_rank))

        # Verify akey enum active.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_akey_enum_active_", False, total_targets, targets_per_rank))

        # Verify dkey enum latency.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_dkey_enum_latency_", True, total_targets, targets_per_rank))

        # Verify akey enum latency.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_akey_enum_latency_", True, total_targets, targets_per_rank))

        # Verify dkey punch active.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_dkey_punch_active_", False, total_targets, targets_per_rank))

        # Verify akey punch active.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_akey_punch_active_", False, total_targets, targets_per_rank))

        # Verify dkey punch latency.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_dkey_punch_latency_", True, total_targets, targets_per_rank))

        # Verify akey punch latency.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_akey_punch_latency_", True, total_targets, targets_per_rank))

        self.report_status(errors)

    def test_pool_tgt_dkey_akey_punch(self):
        """Test punch count for tgt values.

        tgt is related to replication, so the test step is similar to above,
        but we use the replication object class OC_RP_2G1.

        Test Steps:
        1. Write 100 objects.
        2. Insert 2 dkeys per object. Insert 1 akey per dkey. Use OC_RP_2G1.
        3. Punch all akeys and dkeys.
        4. Verify the metrics below.

        --- Metrics Tested ---
        1. engine_pool_ops_tgt_dkey_punch
        Number of dkeys punched. There are 200 dkeys total and we punch them one
        at a time, so we expect this value to sum up to 200.

        2. engine_pool_ops_akey_punch
        Number of akeys punched. There are 200 akeys total and we punch them one
        at a time, so we expect this value to sum up to 200.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=telemetry
        :avocado: tags=DkeyAkeyEnumPunch,test_pool_tgt_dkey_akey_punch
        """
        errors = []
        pool = add_pool(self)
        container = add_container(self, pool)
        container.open()
        obj_count = 100

        # Create objects and dkey/akey in it. Use RP_2G1 for tgt metrics.
        self.write_objects_insert_keys(container, DaosObjClass.OC_RP_2G1, obj_count)

        # Punch the akeys and the dkeys in the objects.
        self.punch_all_keys(obj_count)

        self.telemetry.dmg.verbose = False

        # Obtain and verify the pool target punch metrics
        pool_tgt_dkey_punch = self.telemetry.ENGINE_POOL_OPS_TGT_DKEY_PUNCH_METRICS
        pool_tgt_akey_punch = self.telemetry.ENGINE_POOL_OPS_TGT_AKEY_PUNCH_METRICS
        specific_metrics = [pool_tgt_dkey_punch, pool_tgt_akey_punch]
        pool_out = self.telemetry.get_pool_metrics(
            specific_metrics=specific_metrics)

        # Verify tgt_dkey_punch total is 200.
        tgt_dkey_punch_total = self.sum_values(
            metric_out=pool_out[pool_tgt_dkey_punch])
        if tgt_dkey_punch_total != 200:
            msg = "tgt dkey punch total is not 200! Actual = {}".format(
                tgt_dkey_punch_total)
            errors.append(msg)

        # Verify tgt_akey_punch total is 200.
        tgt_akey_punch_total = self.sum_values(
            metric_out=pool_out[pool_tgt_akey_punch])
        if tgt_akey_punch_total != 200:
            msg = "tgt akey punch total is not 200! Actual = {}".format(
                tgt_akey_punch_total)
            errors.append(msg)

        self.report_status(errors)

    def test_tgt_dkey_akey_punch(self):
        """Test active and latency for tgt punch.

        This case is the same as the metrics 7 and 8 in
        test_dkey_akey_enum_punch() except the metrics have tgt and we need to
        use OC_RP_2G1.

        Test Steps:
        1. Write 100 objects.
        2. Insert 2 dkeys per object. Insert 1 akey per dkey. Use OC_RP_2G1.
        3. Punch all akeys and dkeys.
        4. Verify the metrics below.

        --- Metrics tested ---
        1. tgt dkey punch active and latency.
        engine_io_ops_tgt_dkey_punch_active_max
        engine_io_ops_tgt_dkey_punch_active_mean
        engine_io_ops_tgt_dkey_punch_active_min
        engine_io_ops_tgt_dkey_punch_active_stddev
        engine_io_ops_tgt_dkey_punch_latency_max
        engine_io_ops_tgt_dkey_punch_latency_mean
        engine_io_ops_tgt_dkey_punch_latency_min
        engine_io_ops_tgt_dkey_punch_latency_stddev

        2. tgt akey punch active and latency.
        engine_io_ops_tgt_akey_punch_active_max
        engine_io_ops_tgt_akey_punch_active_mean
        engine_io_ops_tgt_akey_punch_active_min
        engine_io_ops_tgt_akey_punch_active_stddev
        engine_io_ops_tgt_akey_punch_latency_max
        engine_io_ops_tgt_akey_punch_latency_mean
        engine_io_ops_tgt_akey_punch_latency_min
        engine_io_ops_tgt_akey_punch_latency_stddev

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=telemetry
        :avocado: tags=DkeyAkeyEnumPunch,test_tgt_dkey_akey_punch
        """
        errors = []
        pool = add_pool(self)
        container = add_container(self, pool)
        container.open()
        total_targets, targets_per_rank = self.set_num_targets(pool)
        obj_count = 100

        # Object type needs to be OC_RP_2G1 because we're testing tgt.
        self.write_objects_insert_keys(container, DaosObjClass.OC_RP_2G1, obj_count)

        self.punch_all_keys(obj_count)

        self.telemetry.dmg.verbose = False

        # Verify active and latency; metrics 1 and 2. ###
        # Verify tgt dkey punch active.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_tgt_dkey_punch_active_", False, total_targets, targets_per_rank))

        # Verify dkey punch latency.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_tgt_dkey_punch_latency_", True, total_targets, targets_per_rank))

        # Verify akey punch active.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_tgt_akey_punch_active_", False, total_targets, targets_per_rank))

        # Verify akey punch latency.
        errors.extend(
            self.verify_active_latency(
                "engine_io_ops_tgt_akey_punch_latency_", True, total_targets, targets_per_rank))

        self.report_status(errors)
