import pytest
import copy

@pytest.mark.functional
def test_rnr_read_cq_error(cmdline_args):
    from common import ClientServerTest

    if cmdline_args.server_id == cmdline_args.client_id:
        pytest.skip("RNR requires 2 nodes")
        return

    # Older efa kernel driver does not support RNR retry capability
    # and the test will return FI_ENOSYS.
    # Disable the strict mode for this test explicitly to mark it as skipped
    # in this case.
    cmdline_args_copy = copy.copy(cmdline_args)
    cmdline_args_copy.strict_fabtests_mode = False
    test = ClientServerTest(cmdline_args_copy, "fi_efa_rnr_read_cq_error")
    test.run()

packet_type_option_map = {
    "cts" : "-c 0 -S 1048576",
    "readrsp" : "-c 0 -o read -S 4",
    "atomrsp" : "-c 0 -A read -S 4",
    "receipt" : "-c 0 -U -S 4",
    "eager_msgrtm" : "-c 1 -S 4",
    "eager_tagrtm" : "-c 1 -T -S 4",
    "medium_msgrtm" : "-c 1 -S 16384",
    "medium_tagrtm" : "-c 1 -T -S 16384",
    "longcts_msgrtm" : "-c 1 -S 1048576",
    "longcts_tagrtm" : "-c 1 -T -S 1048576",
    "eager_rtw" : "-c 1 -o write -S 4",
    "longcts_rtw" : "-c 1 -o write -S 1048576",
    "short_rtr" : "-c 1 -o read -S 4",
    "longcts_rtr" : "-c 1 -o read -S 1048576",
    "write_rta" : "-c 1 -A write -S 4",
    "fetch_rta" : "-c 1 -A read -S 4",
    "compare_rta" : "-c 1 -A cswap -S 4",
    "dc_eager_msgrtm" : "-c 1 -U -S 4",
    "dc_eager_tagrtm" : "-c 1 -T -U -S 4",
    "dc_medium_msgrtm" : "-c 1 -U -S 16384",
    "dc_medium_tagrtm" : "-c 1 -T -U -S 16384",
    "dc_longcts_msgrtm" : "-c 1 -U -S 1048576",
    "dc_longcts_tagrtm" : "-c 1 -T -U -S 1048576",
    "dc_eager_rtw" : "-c 1 -o write -U -S 4",
    "dc_longcts_rtw" : "-c 1 -o write -U -S 1048576",
    "dc_write_rta": "-c 1 -A write -U -S 4",
    "writedata": "-c 1 -o writedata -S 4"
}

@pytest.mark.functional
@pytest.mark.parametrize("packet_type", packet_type_option_map.keys())
def test_rnr_queue_resend(cmdline_args, packet_type):
    from common import ClientServerTest

    if cmdline_args.server_id == cmdline_args.client_id:
        pytest.skip("RNR test requires 2 nodes")
        return

    # Older efa kernel driver does not support RNR retry capability
    # and the test will return FI_ENOSYS.
    # Disable the strict mode for this test explicitly to mark it as skipped
    # in this case.
    cmdline_args_copy = copy.copy(cmdline_args)
    cmdline_args_copy.strict_fabtests_mode = False
    test = ClientServerTest(cmdline_args_copy,
            "fi_efa_rnr_queue_resend " + packet_type_option_map[packet_type])
    test.run()
