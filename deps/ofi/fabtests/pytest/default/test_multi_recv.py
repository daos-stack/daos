import pytest

@pytest.mark.parametrize("endpoint_type", ["rdm", "msg"])
@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_multi_recv(cmdline_args, iteration_type, endpoint_type):
    from common import ClientServerTest
    command = "fi_multi_recv -e " + endpoint_type
    test = ClientServerTest(cmdline_args, command, iteration_type)
    test.run()
