import pytest

@pytest.mark.unit
def test_rdm_g00n13s(cmdline_args):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_rdm g00n13s", is_negative=True)
    test.run()

@pytest.mark.functional
def test_rdm(cmdline_args, completion_semantic):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm", completion_semantic=completion_semantic)
    test.run()

@pytest.mark.functional
def test_rdm_rma_event(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm_rma_event")
    test.run()

@pytest.mark.functional
def test_rdm_rma_trigger(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm_rma_trigger")
    test.run()

@pytest.mark.functional
def test_rdm_tagged_peek(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm_tagged_peek")
    test.run()

@pytest.mark.functional
def test_rdm_shared_av(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm_shared_av")
    test.run()

@pytest.mark.functional
def test_rdm_bw_functional(cmdline_args, completion_semantic):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_bw -e rdm -v -T 1", completion_semantic=completion_semantic)
    test.run()

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_atomic(cmdline_args, iteration_type, completion_semantic):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm_atomic", iteration_type, completion_semantic)
    test.run()

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_cntr_pingpong(cmdline_args, iteration_type):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm_cntr_pingpong", iteration_type)
    test.run()

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_pingpong(cmdline_args, iteration_type,
                      prefix_type, datacheck_type, completion_semantic):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm_pingpong", iteration_type,
                            completion_semantic, prefix_type, datacheck_type)
    test.run()

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_tagged_pingpong(cmdline_args, iteration_type,
                             datacheck_type, completion_semantic):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm_tagged_pingpong", iteration_type,
                            completion_semantic, datacheck_type=datacheck_type)
    test.run()

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_tagged_bw(cmdline_args, iteration_type, datacheck_type, completion_semantic):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_rdm_tagged_bw", iteration_type,
                            completion_semantic, datacheck_type=datacheck_type)
    test.run()


