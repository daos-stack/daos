#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from mpio_test_base import MpiioTests


# pylint: disable=too-many-ancestors
class Hdf5(MpiioTests):
    """Runs HDF5 test suites.

    :avocado: recursive
    """

    def test_hdf5(self):
        """Jira ID: DAOS-2252.

        Test Description:
            Run HDF5 testphdf5 and t_shapesame provided in HDF5 package. Testing
            various I/O functions provided in HDF5 test suite such as:
                test_fapl_mpio_dup
                test_split_comm_access
                test_page_buffer_access
                test_file_properties
                dataset_writeInd
                dataset_readInd
                dataset_writeAll
                dataset_readAll
                extend_writeInd
                extend_readInd
                extend_writeAll
                extend_readAll
                extend_writeInd2
                none_selection_chunk
                zero_dim_dset
                multiple_dset_write
                multiple_group_write
                multiple_group_read
                compact_dataset
                collective_group_write
                independent_group_read
                big_dataset
                coll_chunk[1-10]
                coll_irregular_cont_write
                coll_irregular_cont_read
                coll_irregular_simple_chunk_write
                coll_irregular_simple_chunk_read
                coll_irregular_complex_chunk_write
                coll_irregular_complex_chunk_read
                null_dataset
                io_mode_confusion
                rr_obj_hdr_flush_confusion
                chunk_align_bug_1
                lower_dim_size_comp_test
                link_chunk_collective_io_test
                actual_io_mode_tests
                no_collective_cause_tests
                test_plist_ed
                file_image_daisy_chain_test
                test_dense_attr
                test_partial_no_selection_coll_md_read

        :avocado: tags=all,daily_regression,hw,small,mpio,llnlmpi4pyhdf5,hdf5
        """
        test_repo = self.params.get("hdf5", '/run/test_repo/')
        self.run_test(test_repo, "hdf5")
