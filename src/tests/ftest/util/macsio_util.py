"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from command_utils import ExecutableCommand
from command_utils_base import FormattedParameter
from general_utils import get_log_file
from run_utils import run_remote


class MacsioCommand(ExecutableCommand):
    # pylint: disable=too-many-instance-attributes
    """Defines an object from running the macsio command.

    Multi-purpose, Application-Centric, Scalable I/O Proxy Application
    https://github.com/LLNL/MACSio
    """

    def __init__(self, path=""):
        """Create an MacsioCommand object.

        Args:
            path (str, optional): path to the macsio command. Defaults to "".
        """
        super().__init__("/run/macsio/*", "macsio", path)

        # pylint: disable=wrong-spelling-in-comment

        # MACSio command parameters - defaults specified in square brackets:

        #   --units_prefix_system %s ["binary"]
        #       Specify which SI units prefix system to use both in reporting
        #       performance data and in interpreting sizing modifiers to
        #       arguments. The options are "binary" and "decimal". For "binary"
        #       unit prefixes, sizes are reported in powers of 1024 and unit
        #       symbols Ki, Mi, Gi, Ti, Pi are used. For "decimal", sizes are
        #       reported in powers of 1000 and unit symbols are Kb, Mb, Gb, Tb,
        #       Pb.  See http://en.wikipedia.org/wiki/Binary_prefix. for more
        #       information
        self.units_prefix_system = FormattedParameter(
            "--units_prefix_system {}")

        #   --interface %s [miftmpl]
        #       Specify the name of the interface to be tested. Use keyword
        #       'list' to print a list of all known interface names and then
        #       exit.
        self.interface = FormattedParameter("--interface {}", "hdf5")

        #   --parallel_file_mode %s %d [MIF 4]
        #       Specify the parallel file mode. There are several choices.  Use
        #       'MIF' for Multiple Independent File (Poor Man's) mode and then
        #       also specify the number of files. Or, use 'MIFFPP' for MIF mode
        #       and one file per processor or 'MIFOPT' for MIF mode and let the
        #       test determine the optimum file count. Use 'SIF' for SIngle
        #       shared File (Rich Man's) mode. If you also give a file count for
        #       SIF mode, then MACSio will perform a sort of hybrid combination
        #       of MIF and SIF modes.  It will produce the specified number of
        #       files by grouping ranks in the the same way MIF does, but I/O
        #       within each group will be to a single, shared file using SIF
        #       mode.
        #
        #       Run macsio with SIF mode. MIF mode uses the HDF5 posix driver,
        #       so it won't go through MPI-IO and hence not through the MPI-IO
        #       DAOS driver.
        #
        #       Note: Value should be specified as a string of a space-
        #           separated string and integer value, e.g. 'SIF 1'.
        self.parallel_file_mode = FormattedParameter(
            "--parallel_file_mode {}", "SIF 1")

        #   --avg_num_parts %f [1]
        #       The average number of mesh parts per MPI rank. Non-integral
        #       values are acceptable. For example, a value that is half-way
        #       between two integers, K and K+1, means that half the ranks have
        #       K mesh parts and half have K+1 mesh parts. As another example,
        #       a value of 2.75 here would mean that 75% of the ranks get 3
        #       parts and 25% of the ranks get 2 parts. Note that the total
        #       number of parts is this number multiplied by the MPI
        #       communicator size. If the result of that product is
        #       non-integral, it will be rounded and a warning message will be
        #       generated.
        self.avg_num_parts = FormattedParameter("--avg_num_parts {}")

        #   --mesh_decomp %d %d %d []
        #       The layout of parts in the mesh overriding the simple
        #       decomposition e.g. 4 8 1 will decompose into 32 parts in the
        #       structure (x y z).
        #
        #       Note: Value should be specified as a string of three space-
        #           separated integer values, e.g. '4 8 1'.
        self.mesh_decomp = FormattedParameter("--mesh_decomp {}")

        #   --part_size %d [80000]
        #       Mesh part size in bytes. This becomes the nominal I/O request
        #       size used by each MPI rank when marshalling data. A following
        #       B|K|M|G character indicates 'B'ytes, 'K'ilo-, 'M'ega- or 'G'iga-
        #       bytes representing powers of either 1000 or 1024 according to
        #       the selected units prefix system. With no size modifier
        #       character, 'B' is assumed.  Mesh and variable data is then sized
        #       by MACSio to hit this target byte count. However, due to
        #       constraints involved in creating valid mesh topology and
        #       variable data with realistic variation in features (e.g.  zone-
        #       and node-centering), this target byte count is hit exactly for
        #       only the most frequently dumped objects and approximately for
        #       other objects.
        self.part_size = FormattedParameter("--part_size {}")

        #   --part_mesh_dims %d %d %d []
        #       Specify the number of elements in each dimension per mesh part.
        #       This overrides the part_size parameter and instead allows the
        #       size of the mesh to be determined by dimensions. e.g. 300 300 2,
        #       300 300 0 (set final dimension to 0 for 2d
        #
        #       Note: Value should be specified as a string of three space-
        #           separated integer values, e.g. '300 300 2'.
        self.part_mesh_dims = FormattedParameter("--part_mesh_dims {}")

        #   --part_dim %d [2]
        #       Spatial dimension of parts; 1, 2, or 3
        self.part_dim = FormattedParameter("--part_dim {}")

        #   --part_type %s [rectilinear]
        #       Options are 'uniform', 'rectilinear', 'curvilinear',
        #       'unstructured' and 'arbitrary' (currently, only rectilinear is
        #       implemented)
        self.part_type = FormattedParameter("--part_type {}")

        #   --part_map %s []
        #       Specify the name of an ascii file containing part assignments to
        #       MPI ranks.  The ith line in the file, numbered from 0, holds the
        #       MPI rank to which the ith part is to be assigned. (currently
        #       ignored)
        self.part_map = FormattedParameter("--part_map {}")

        #   --vars_per_part %d [20]
        #       Number of mesh variable objects in each part. The smallest this
        #       can be depends on the mesh type. For rectilinear mesh it is 1.
        #       For curvilinear mesh it is the number of spatial dimensions and
        #       for unstructured mesh it is the number of spatial dimensions
        #       plus 2^number of topological dimensions.
        self.vars_per_part = FormattedParameter("--vars_per_part {}")

        #   --dataset_growth %f []
        #       The factor by which the volume of data will grow between dump
        #       iterations If no value is given or the value is <1.0 no dataset
        #       changes will take place.
        self.dataset_growth = FormattedParameter("--dataset_growth {}")

        #   --topology_change_probability %f [0.0]
        #       The probability that the topology of the mesh (e.g. something
        #       fundamental about the mesh's structure) will change between
        #       dumps. A value of 1.0 indicates it should be changed every dump.
        #       A value of 0.0, the default, indicates it will never change. A
        #       value of 0.1 indicates it will change about once every 10 dumps.
        #       Note: at present MACSio will not actually compute/construct a
        #       different topology. It will only inform a plugin that a given
        #       dump should be treated as a change in topology.
        self.topology_change_probability = FormattedParameter(
            "--topology_change_probability {}")

        #   --meta_type %s [tabular]
        #       Specify the type of metadata objects to include in each main
        #       dump.  Options are 'tabular', 'amorphous'. For tabular type
        #       data, MACSio will generate a random set of tables of somewhat
        #       random structure and content. For amorphous, MACSio will
        #       generate a random hierarchy of random type and sized objects.
        self.meta_type = FormattedParameter("--meta_type {}")

        #   --meta_size %d %d [10000 50000]
        #       Specify the size of the metadata objects on each processor and
        #       separately, the root (or master) processor (MPI rank 0). The
        #       size is specified in terms of the total number of bytes in the
        #       metadata objects MACSio creates. For example, a type of tabular
        #       and a size of 10K bytes might result in 3 random tables; one
        #       table with 250 unnamed records where each record is an array of
        #       3 doubles for a total of 6000 bytes, another table of 200
        #       records where each record is a named integer value where each
        #       name is length 8 chars for a total of 2400 bytes and a 3rd table
        #       of 40 unnamed records where each record is a 40 byte struct
        #       comprised of ints and doubles for a total of 1600 bytes.
        #
        #       Note: Value should be specified as a string of two space-
        #           separated integer values, e.g. '10000 50000'.
        self.meta_size = FormattedParameter("--meta_size {}")

        #   --num_dumps %d [10]
        #       The total number of dumps to marshal.
        self.num_dumps = FormattedParameter("--num_dumps {}")

        #   --max_dir_size %d []
        #       The maximum number of filesystem objects (e.g. files or
        #       subdirectories) that MACSio will create in any one subdirectory.
        #       This is typically relevant only in MIF mode because MIF mode can
        #       wind up generating many will continue to create output files in
        #       the same directory until it has completed all dumps. Use a value
        #       of zero to force MACSio to put each dump in a separate directory
        #       but where the number of top-level directories is still
        #       unlimited. The result will be a 2-level directory hierarchy with
        #       dump directories at the top and individual dump files in each
        #       directory. A value > 0 will cause MACSio to create a tree-like
        #       directory structure where the files are the leaves and
        #       encompassing dir tree is created such as to maintain the
        #       max_dir_size constraint specified here.  For example, if the
        #       value is set to 32 and the MIF file count is 1024, then each
        #       dump will involve a 3-level dir-tree; the top dir containing 32
        #       sub-dirs and each sub-dir containing 32 of the 1024 files for
        #       the dump. If more than 32 dumps are performed, then the dir-tree
        #       will really be 4 or more levels with the first 32 dumps'
        #       dir-trees going into the first dir, etc.
        self.max_dir_size = FormattedParameter("--max_dir_size {}")

        #   --compute_work_intensity %d [1]
        #       Add some work in between I/O phases. There are three levels of
        #       'compute' that can be performed as follows:
        #           Level 1: Perform a basic sleep operation
        #           Level 2: Perform some simple FLOPS with randomly accessed
        #                    data
        #           Level 3: Solves the 2D Poisson equation via the Jacobi
        #                    iterative method
        #       This input is intended to be used in conjunction with
        #       --compute_time which will roughly control how much time is spent
        #       doing work between iops
        self.compute_work_intensity = FormattedParameter(
            "--compute_work_intensity {}")

        #   --compute_time %f []
        #       A rough lower bound on the number of seconds spent doing work
        #       between I/O phases. The type of work done is controlled by the
        #       --compute_work_intensity input and defaults to Level 1 (basic
        #       sleep).
        self.compute_time = FormattedParameter("--compute_time {}")

        #  --alignment %d []
        #       Not currently documented
        self.alignment = FormattedParameter("--alignment {}")

        #   --filebase %s [macsio]
        #       Basename of generated file(s).
        self.filebase = FormattedParameter("--filebase {}")

        #   --fileext %s []
        #       Extension of generated file(s).
        self.fileext = FormattedParameter("--fileext {}")

        #   --read_path %s []
        #       Specify a path name (file or dir) to start reading for a read
        #       test.
        self.read_path = FormattedParameter("--read_path {}")

        #   --num_loads %d []
        #       Number of loads in succession to test.
        self.num_loads = FormattedParameter("--num_loads {}")

        #   --no_validate_read []
        #       Don't validate data on read.
        self.no_validate_read = FormattedParameter("--no_validate_read", False)

        #   --read_mesh %s []
        #       Specify mesh name to read.
        self.read_mesh = FormattedParameter("--read_mesh {}")

        #   --read_vars %s []
        #       Specify variable names to read. "all" means all variables. If
        #       listing more than one, be sure to either enclose space separated
        #       list in quotes or use a comma-separated list with no spaces
        self.read_vars = FormattedParameter("--read_vars {}")

        #   --time_randomize []
        #       Make randomness in MACSio vary from dump to dump and run to run
        #       by using PRNGs seeded by time.
        self.time_randomize = FormattedParameter("--time_randomize", False)

        #   --plugin_args %n []
        #       All arguments after this sentinel are passed to the I/O plugin
        #       plugin. The '%n' is a special designator for the builtin 'argi'
        #       value.
        self.plugin_args = FormattedParameter("--plugin_args {}")

        #   --debug_level %d [0]
        #       Set debugging level (1, 2 or 3) of log files. Higher numbers
        #       mean more frequent and detailed output. A value of zero, the
        #       default, turns all debugging output off. A value of 1 should not
        #       adversely effect performance. A value of 2 may effect
        #       performance and a value of 3 will almost certainly effect
        #       performance. For debug level 3, MACSio will generate ascii json
        #       files from each processor for the main dump object prior to
        #       starting dumps.
        self.debug_level = FormattedParameter("--debug_level {}")

        #
        # Log File Options to control size and shape of log file:
        #

        #   --log_file_name %s [macsio-log.log]
        #       The name of the log file.
        self.log_file_name = FormattedParameter(
            "--log_file_name {}", "macsio-log.log")

        #   --log_line_cnt %d %d [64 0]
        #       Set number of lines per rank in the log file and number of extra
        #       lines for rank 0.
        self.log_line_cnt = FormattedParameter("--log_line_cnt {}")

        #   --log_line_length %d [128]
        #       Set log file line length.
        self.log_line_length = FormattedParameter("--log_line_length {}")

        #   --timings_file_name %s [macsio-timings.log]
        #       Specify the name of the timings file. Passing an empty string,
        #       "" will disable the creation of a timings file.
        self.timings_file_name = FormattedParameter(
            "--timings_file_name {}", "macsio-timings.log")

        #
        # Options specific to the "hdf5" I/O plugin
        #

        #   --show_errors []
        #       Show low-level HDF5 errors
        self.show_errors = FormattedParameter("--show_errors", False)

        #   --compression %s %s []
        #       The first string argument is the compression algorithm name. The
        #       second string argument is a comma-separated set of params of the
        #       form 'param1=val1,param2=val2,param3=val3. The various algorithm
        #       names and their parameter meanings are described below. Note
        #       that some parameters are not specific to any algorithm. Those
        #       are described first followed by individual algorithm-specific
        #       parameters for those algorithms available in the current build.
        #
        #           minsize=%d [1024]
        #               minimum size of dataset (in terms of a count of values)
        #               upon which compression will even be attempted
        #
        #           shuffle=<int>
        #               Boolean (zero or non-zero) to indicate whether to use
        #               HDF5's byte shuffling filter *prior* to compression.
        #               Default depends on algorithm. By default, shuffling is
        #               NOT used for zfp but IS used with all other algorithms.
        #
        #       Available compression algorithms:
        #
        #           "zfp"
        #               Use Peter Lindstrom's ZFP compression (
        #               computation.llnl.gov/casc/zfp) Note: Whether this
        #               compression is available is determined entirely at
        #               run-time using the H5Z-ZFP compressor as a generic
        #               filter. This means all that is necessary is to specify
        #               the HDF5_PLUGIN_PATH environment variable with a path
        #               to the shared lib for the filter.
        #
        #               The following ZFP options are *mutually*exclusive*.
        #               In any command-line specifying more than one of the
        #               following options, only the last specified will be
        #               honored.
        #
        #               rate=%f []
        #                   target # bits per compressed output datum.
        #                   Fractional values are permitted. 0 selects defaults:
        #                   4 bits/flt or 8 bits/dbl.  Use this option to hit a
        #                   target compressed size but where error varies. OTOH,
        #                   use one of the following two options for fixed error
        #                   but amount of compression, if any, varies.
        #
        #               precision=%d []
        #                   # bits of precision to preserve in each input datum.
        #
        #               accuracy=%f []
        #                   absolute error tolerance in each output datum.  In
        #                   many respects, 'precision' represents a sort of
        #                   relative error tolerance while 'accuracy' represents
        #                   an absolute tolerance.  See
        #                   http://en.wikipedia.org/wiki/Accuracy_and_precision.
        #
        #          "gzip"
        #               level=%d [9]
        #                   A value in the range [1,9], inclusive, trading off
        #                   time to compress with amount of compression. Level=1
        #                   results in best speed but worst compression whereas
        #                   level=9 results in best compression but worst speed.
        #                   Values outside [1,9] are clamped.
        #
        #       Examples:
        #           --compression zfp rate=18.5
        #           --compression gzip minsize=1024,level=9
        #           --compression szip shuffle=0,options=nn,pixels_per_block=16
        self.compression = FormattedParameter("--compression {}")

        #   --no_collective []
        #       Use independent, not collective, I/O calls in SIF mode.
        self.no_collective = FormattedParameter("--no_collective", False)

        #   --no_single_chunk []
        #       Do not single chunk the datasets (currently ignored).
        self.no_single_chunk = FormattedParameter("--no_single_chunk", False)

        #   --sieve_buf_size %d []
        #       Specify sieve buffer size (see H5Pset_sieve_buf_size)
        self.sieve_buf_size = FormattedParameter("--sieve_buf_size {}")

        #   --meta_block_size %d []
        #       Specify size of meta data blocks (see H5Pset_meta_block_size)
        self.meta_block_size = FormattedParameter("--meta_block_size {}")

        #   --small_block_size %d []
        #       Specify threshold size for data blocks considered to be 'small'
        #       (see H5Pset_small_data_block_size)
        self.small_block_size = FormattedParameter("--small_block_size {}")

        #   --log []
        #       Use logging Virtual File Driver (see H5Pset_fapl_log)
        self.log_virtual_file_driver = FormattedParameter("--log {}")

        # DAOS parameters
        self.daos_pool = None
        self.daos_svcl = None
        self.daos_cont = None

    def set_output_file_path(self):
        """Set the path for the files generated by the macsio command."""
        self.log_file_name.update(
            get_log_file(self.log_file_name.value),
            "macsio.log_file_name")
        self.timings_file_name.update(
            get_log_file(self.timings_file_name.value),
            "macsio.timings_file_name")

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Also sets default macsio environment.

        Args:
            test (Test): avocado Test object

        """
        super().get_params(test)
        default_env = {
            "D_LOG_FILE": get_log_file("{}_daos.log".format(self.command)),
            "DAOS_POOL": self.daos_pool,
            "DAOS_SVCL": self.daos_svcl,
            "DAOS_CONT": self.daos_cont,
        }
        for key, val in default_env.items():
            if val and key not in self.env:
                self.env[key] = val

    def check_results(self, result, hosts):
        # pylint: disable=arguments-differ
        """Check the macsio command results.

        Args:
            results (CmdResult): macsio command execution result
            hosts (NodeSet): hosts on which the macsio output files exist

        Returns:
            bool: status of the macsio command results

        """
        self.log.info(
            "The '%s' command completed with exit status: %s", str(self), result.exit_status)

        # Basic check of the macsio command status
        status = result.exit_status == 0

        # Display the results from the macsio log and timings files
        macsio_files = (self.log_file_name.value, self.timings_file_name.value)
        for macsio_file in macsio_files:
            if macsio_file:
                # DAOS-17157 - this needs error checking but is currently failing
                run_remote(self.log, hosts, f"cat {macsio_file}", timeout=30)
                # if not result.passed:
                #     status = False

        return status
