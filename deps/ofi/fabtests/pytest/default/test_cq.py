import pytest

@pytest.mark.unit
def test_cq(cmdline_args):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_cq_test")
    test.run()

@pytest.mark.functional
@pytest.mark.parametrize("operation_type", ["senddata", "writedata"])
@pytest.mark.parametrize("endpoint_type", ["msg", "rdm", "dgram"])
def test_cq_data(cmdline_args, operation_type, endpoint_type):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_cq_data -e " + endpoint_type + " -o " + operation_type)
    test.run()
