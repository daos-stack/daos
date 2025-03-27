import pytest

from efa.efa_common import has_gdrcopy


# TODO Expand this test to run on all memory types (and rename)
@pytest.mark.serial
@pytest.mark.functional
@pytest.mark.cuda_memory
@pytest.mark.parametrize("fabtest_name,cntrl_env_var", [("fi_rdm_tagged_bw", "FI_EFA_INTER_MIN_READ_MESSAGE_SIZE"), ("fi_rma_bw", "FI_EFA_INTER_MIN_READ_WRITE_SIZE")])
def test_transfer_with_read_protocol_cuda(cmdline_args, fabtest_name, cntrl_env_var):
    """
    Verify that the read protocol is used for a 1024 byte message when the env variable
    switches are set to force the read protocol at 1000 bytes.
    """
    import copy
    from common import has_cuda, has_hmem_support
    from efa.efa_common import efa_run_client_server_test, efa_retrieve_hw_counter_value

    if cmdline_args.server_id == cmdline_args.client_id:
        pytest.skip("No read for intra-node communication")

    if (not has_hmem_support(cmdline_args, cmdline_args.client_id) or
        not has_hmem_support(cmdline_args, cmdline_args.server_id)):
        pytest.skip("Client and server both need hmem support for RDMA Read test")

    if not has_cuda(cmdline_args.client_id) or not has_cuda(cmdline_args.server_id):
        pytest.skip("Client and server both need a Cuda device")

    message_size = 1024

    cmdline_args_copy = copy.copy(cmdline_args)
    cmdline_args_copy.append_environ("FI_EFA_USE_DEVICE_RDMA=1")
    cmdline_args_copy.append_environ(f"{cntrl_env_var}=1000")
    cmdline_args_copy.append_environ("FI_EFA_RUNT_SIZE=0")

    # wrs stands for work requests
    server_read_wrs_before_test = efa_retrieve_hw_counter_value(cmdline_args.server_id, "rdma_read_wrs")
    if server_read_wrs_before_test is None:
        pytest.skip("No HW counter support")
        return
    server_read_bytes_before_test = efa_retrieve_hw_counter_value(cmdline_args.server_id, "rdma_read_bytes")

    efa_run_client_server_test(cmdline_args_copy,
                               fabtest_name,
                               iteration_type="1",
                               completion_semantic="transmit_complete",
                               memory_type="cuda_to_cuda",
                               message_size=message_size,
                               warmup_iteration_type="0")

    server_read_wrs_after_test = efa_retrieve_hw_counter_value(cmdline_args.server_id, "rdma_read_wrs")
    server_read_bytes_after_test = efa_retrieve_hw_counter_value(cmdline_args.server_id, "rdma_read_bytes")

    bytes_read = server_read_bytes_after_test - server_read_bytes_before_test
    # Check that the READ protocol was the only protocol used.
    # The hw counter should record:
    # - The exact message size if gdrcopy is enabled, or
    # - More if gdrcopy is disabled and localread is used.
    if (has_gdrcopy(cmdline_args.server_id)):
        assert bytes_read == message_size
        assert server_read_wrs_after_test == server_read_wrs_before_test + 1
    else:
        assert bytes_read > message_size
        assert server_read_wrs_after_test > server_read_wrs_before_test + 1
