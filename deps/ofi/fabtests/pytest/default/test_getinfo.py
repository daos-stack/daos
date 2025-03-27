import pytest

@pytest.mark.unit
def test_getinfo(cmdline_args, server_address, good_address):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_getinfo_test -s " + server_address + " " + good_address)
    test.run()
