#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import ctypes

from telemetry_test_base import TestWithTelemetry
from pydaos.raw import DaosContainer, IORequest, DaosObjClass
from general_utils import create_string_buffer


class DkeyAkeyEnumPunch(TestWithTelemetry):
    """
    Test Class Description:
    Verify enum and punch values for dkey and akey.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DkeyAkeyEnumPunch object."""
        super().__init__(*args, **kwargs)
        self.total_targets = None
        self.targets_per_rank = None
        self.errors = []
        self.ioreqs = []
        self.dkeys_a = []
        self.dkeys_b = []
        self.dkey_strs_a = []
        self.dkey_strs_b = []
        self.akey_strs = []
        self.obj_count = 100

    def set_num_targets(self):
        """Define total number of targets and targets per rank from pool query.

        Sometimes the total number of targets and targets per rank are different
        from what we specify in the server config.
        """
        self.pool.set_query_data()
        self.total_targets = self.pool.query_data["response"]["total_targets"]

        # Calculate targets per rank from the total targets.
        server_count = len(self.server_managers[0].hosts)
        self.targets_per_rank = int(self.total_targets / server_count)

    def write_objects_insert_keys(self, container, objtype):
        """Write objects and insert dkeys and akeys in them.

        Args:
            container (DaosContainer): Container.
            objtype (str): Object class.
        """
        for i in range(self.obj_count):
            self.ioreqs.append(IORequest(
                context=self.context, container=container, obj=None,
                objtype=objtype))

            # Prepare 2 dkeys and 1 akey in each dkey. Use the same akey for
            # both dkeys.
            dkey_str_a = b"Sample dkey A %d" % i
            dkey_str_b = b"Sample dkey B %d" % i
            akey_str = b"Sample akey %d" % i
            data_str = b"Sample data %d" % i
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

    def punch_all_keys(self):
        """Punch all dkeys and akeys in the objects.
        """
        for i in range(self.obj_count):
            self.ioreqs[i].obj.punch_akeys(
                0, dkey=self.dkey_strs_a[i], akeys=[self.akey_strs[i]])
            self.ioreqs[i].obj.punch_akeys(
                0, dkey=self.dkey_strs_b[i], akeys=[self.akey_strs[i]])
            # Punch dkeys one at a time. DAOS-8945
            self.ioreqs[i].obj.punch_dkeys(0, dkeys=[self.dkey_strs_a[i]])
            self.ioreqs[i].obj.punch_dkeys(0, dkeys=[self.dkey_strs_b[i]])
            if i % 10 == 0:
                self.log.info("Keys punched %d", i)

    def verify_active_latency(self, prefix, test_latency):
        """Call the dmg telemetry command with given prefix and verify.

        Obtain and verify the io metrics 1 to 4 in test_dkey_akey_enum_punch()

        Args:
            prefix (str): Metrics prefix for the metric that has min, max,
                mean, or stddev at the end.
            test_latency (bool): Whether to verify the latency metric.
        """
        metrics = self.get_min_max_mean_stddev(
            prefix=prefix, total_targets=self.total_targets,
            targets_per_rank=self.targets_per_rank)

        self.errors.extend(
            self.verify_stats(
                enum_metrics=metrics, metric_prefix=prefix,
                test_latency=test_latency))

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
        :avocado: tags=dkey_akey_enum_punch,test_dkey_akey_enum_punch
        """
        self.add_pool()

        self.set_num_targets()

        container = DaosContainer(self.context)
        container.create(self.pool.pool.handle)
        container.open()

        # Object type needs to be OC_S1 so that the objects are spread across
        # all targets.
        self.write_objects_insert_keys(
            container=container, objtype=DaosObjClass.OC_S1)

        # Call list_dkey() and list_akey() on each object.
        for i in range(self.obj_count):
            _ = self.ioreqs[i].list_dkey()
            _ = self.ioreqs[i].list_akey(dkey=self.dkeys_a[i])
            _ = self.ioreqs[i].list_akey(dkey=self.dkeys_b[i])

        self.punch_all_keys()

        self.telemetry.dmg.verbose = False

        ### Obtain and verify the io metrics 1 to 4. ###
        # engine_pool_ops_dkey_enum
        pool_dkey_enum = self.telemetry.ENGINE_POOL_METRICS[5]
        # engine_pool_ops_akey_enum
        pool_akey_enum = self.telemetry.ENGINE_POOL_METRICS[2]
        # engine_pool_ops_dkey_punch
        pool_dkey_punch = self.telemetry.ENGINE_POOL_METRICS[6]
        # engine_pool_ops_akey_punch
        pool_akey_punch = self.telemetry.ENGINE_POOL_METRICS[3]
        specific_metrics = [
            pool_dkey_enum, pool_akey_enum,
            pool_dkey_punch, pool_akey_punch,
        ]
        pool_out = self.telemetry.get_pool_metrics(
            specific_metrics=specific_metrics)

        # Verify dkey_enum total is 100.
        dkey_enum_total = self.sum_values(metric_out=pool_out[pool_dkey_enum])
        if dkey_enum_total != 100:
            msg = "dkey enum total is not 100! Actual = {}".format(
                dkey_enum_total)
            self.errors.append(msg)

        # Verify akey_enum total is 200.
        akey_enum_total = self.sum_values(metric_out=pool_out[pool_akey_enum])
        if akey_enum_total != 200:
            msg = "akey enum total is not 200! Actual = {}".format(
                akey_enum_total)
            self.errors.append(msg)

        # Verify dkey_punch total is 200.
        dkey_punch_total = self.sum_values(metric_out=pool_out[pool_dkey_punch])
        if dkey_punch_total != 200:
            msg = "dkey punch total is not 200! Actual = {}".format(
                dkey_punch_total)
            self.errors.append(msg)

        # Verify akey_punch total is 200.
        akey_punch_total = self.sum_values(metric_out=pool_out[pool_akey_punch])
        if akey_punch_total != 200:
            msg = "akey punch total is not 200! Actual = {}".format(
                akey_punch_total)
            self.errors.append(msg)

        ### Verify active and latency; metrics 5 to 8. ###
        # Verify dkey enum active.
        self.verify_active_latency(
            prefix="engine_io_ops_dkey_enum_active_", test_latency=False)

        # Verify akey enum active.
        self.verify_active_latency(
            prefix="engine_io_ops_akey_enum_active_", test_latency=False)

        # Verify dkey enum latency.
        self.verify_active_latency(
            prefix="engine_io_ops_dkey_enum_latency_", test_latency=True)

        # Verify akey enum latency.
        self.verify_active_latency(
            prefix="engine_io_ops_akey_enum_latency_", test_latency=True)

        # Verify dkey punch active.
        self.verify_active_latency(
            prefix="engine_io_ops_dkey_punch_active_", test_latency=False)

        # Verify akey punch active.
        self.verify_active_latency(
            prefix="engine_io_ops_akey_punch_active_", test_latency=False)

        # Verify dkey punch latency.
        self.verify_active_latency(
            prefix="engine_io_ops_dkey_punch_latency_", test_latency=True)

        # Verify akey punch latency.
        self.verify_active_latency(
            prefix="engine_io_ops_akey_punch_latency_", test_latency=True)

        if self.errors:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(self.errors)))

        container.destroy()
        self.pool.destroy(disconnect=0)

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
        :avocado: tags=pool_tgt_dkey_akey_punch,test_pool_tgt_dkey_akey_punch
        """
        self.add_pool()

        self.set_num_targets()

        container = DaosContainer(self.context)
        container.create(self.pool.pool.handle)
        container.open()

        # Create objects and dkey/akey in it. Use RP_2G1 for tgt metrics.
        self.write_objects_insert_keys(
            container=container, objtype=DaosObjClass.OC_RP_2G1)

        # Punch the akeys and the dkeys in the objects.
        self.punch_all_keys()

        self.telemetry.dmg.verbose = False

        ### Obtain and verify the pool metrics 1 and 2 ###
        pool_tgt_dkey_punch = self.telemetry.ENGINE_POOL_METRICS[21]
        pool_tgt_akey_punch = self.telemetry.ENGINE_POOL_METRICS[20]
        specific_metrics = [pool_tgt_dkey_punch, pool_tgt_akey_punch]
        pool_out = self.telemetry.get_pool_metrics(
            specific_metrics=specific_metrics)

        # Verify tgt_dkey_punch total is 200.
        tgt_dkey_punch_total = self.sum_values(
            metric_out=pool_out[pool_tgt_dkey_punch])
        if tgt_dkey_punch_total != 200:
            msg = "tgt dkey punch total is not 200! Actual = {}".format(
                tgt_dkey_punch_total)
            self.errors.append(msg)

        # Verify tgt_akey_punch total is 200.
        tgt_akey_punch_total = self.sum_values(
            metric_out=pool_out[pool_tgt_akey_punch])
        if tgt_akey_punch_total != 200:
            msg = "tgt akey punch total is not 200! Actual = {}".format(
                tgt_akey_punch_total)
            self.errors.append(msg)

        if self.errors:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(self.errors)))

        container.destroy()
        self.pool.destroy(disconnect=0)

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
        :avocado: tags=tgt_dkey_akey_punch,test_tgt_dkey_akey_punch
        """
        self.add_pool()

        self.set_num_targets()

        container = DaosContainer(self.context)
        container.create(self.pool.pool.handle)
        container.open()

        # Object type needs to be OC_RP_2G1 because we're testing tgt.
        self.write_objects_insert_keys(
            container=container, objtype=DaosObjClass.OC_RP_2G1)

        self.punch_all_keys()

        self.telemetry.dmg.verbose = False

        ### Verify active and latency; metrics 1 and 2. ###
        # Verify tgt dkey punch active.
        self.verify_active_latency(
            prefix="engine_io_ops_tgt_dkey_punch_active_", test_latency=False)

        # Verify dkey punch latency.
        self.verify_active_latency(
            prefix="engine_io_ops_tgt_dkey_punch_latency_", test_latency=True)

        # Verify akey punch active.
        self.verify_active_latency(
            prefix="engine_io_ops_tgt_akey_punch_active_", test_latency=False)

        # Verify akey punch latency.
        self.verify_active_latency(
            prefix="engine_io_ops_tgt_akey_punch_latency_", test_latency=True)

        if self.errors:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(self.errors)))

        container.destroy()
        self.pool.destroy(disconnect=0)
