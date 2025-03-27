import pytest

@pytest.mark.unit
def test_av(cmdline_args, server_address, good_address):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_av_test -g " + good_address + " -n 1 -s " + server_address)
    test.run()

@pytest.mark.functional
@pytest.mark.parametrize("endpoint_type", ["rdm", "dgram"])
def test_av_xfer(cmdline_args, endpoint_type):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_av_xfer -e " + endpoint_type)
    test.run()
