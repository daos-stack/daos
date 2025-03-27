import pytest


@pytest.mark.functional
@pytest.mark.parametrize("environment_variable", ["FI_EFA_FORK_SAFE", "RDMAV_FORK_SAFE"])
def test_fork_support(cmdline_args, completion_semantic, environment_variable):
    from common import ClientServerTest
    import copy
    cmdline_args_copy = copy.copy(cmdline_args)

    cmdline_args_copy.append_environ("{}=1".format(environment_variable))
    test = ClientServerTest(cmdline_args_copy, "fi_rdm_tagged_bw -K",
                            completion_semantic=completion_semantic,
                            datacheck_type="with_datacheck")
    test.run()

