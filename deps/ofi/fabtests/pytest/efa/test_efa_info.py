import pytest
from common import UnitTest
from efa_common import efa_retrieve_gid

@pytest.mark.unit
def test_efa_info(cmdline_args):
    test = UnitTest(cmdline_args, "fi_efa_info_test")
    test.run()

@pytest.mark.unit
def test_comm_getinfo(cmdline_args):
    gid = efa_retrieve_gid(cmdline_args.server_id)

    # use GID as source address and dest address
    test = UnitTest(cmdline_args, f"fi_getinfo_test -s {gid} {gid}")
    test.run()
