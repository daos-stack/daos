import pytest

@pytest.mark.parametrize("operation_type", ["writedata", "write"])
@pytest.mark.parametrize("endpoint_type", ["msg", "rdm"])
@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rma_pingpong(cmdline_args, iteration_type, endpoint_type, operation_type, completion_semantic):
    from common import ClientServerTest

    command = "fi_rma_pingpong"
    command = command + " -e " + endpoint_type
    command = command + " -o " + operation_type
    test = ClientServerTest(cmdline_args, command, iteration_type,
                            completion_semantic=completion_semantic)
    test.run()

