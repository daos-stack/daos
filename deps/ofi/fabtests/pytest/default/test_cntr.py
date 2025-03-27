import pytest

@pytest.mark.unit
def test_cntr(cmdline_args):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_cntr_test")
    test.run()
