package io.daos.fs.hadoop;

import org.apache.commons.lang.StringUtils;
import org.apache.hadoop.conf.Configuration;
import org.junit.internal.AssumptionViolatedException;

import java.io.IOException;
import java.net.URI;

/**
 *
 */
public class DaosHadoopTestUtils {
  private static Configuration configuration;
  public static final String TEST_FS_DAOS_NAME = "test.fs.daos.name";

  private DaosHadoopTestUtils(){}

  public static DaosFileSystem createTestFileSystem(Configuration conf)throws IOException{
    DaosFileSystem daosFileSystem = new DaosFileSystem();
    configuration = conf;
    daosFileSystem.initialize(getURI(configuration), configuration);
    return daosFileSystem;
  }

  public static Configuration getConfiguration(){
    return configuration!=null ? configuration:null;
  }

  private static URI getURI(Configuration conf) {
    String fsname = conf.getTrimmed(
            DaosHadoopTestUtils.TEST_FS_DAOS_NAME, "daos://192.168.2.1:23456/");

    boolean liveTest = !StringUtils.isEmpty(fsname);
    URI testURI = null;
    if (liveTest) {
      testURI = URI.create(fsname);
      liveTest = testURI.getScheme().equals(Constants.DAOS_SCHEMA);
    }

    if (!liveTest) {
      throw new AssumptionViolatedException("No test filesystem in "
          + DaosHadoopTestUtils.TEST_FS_DAOS_NAME);
    }
    return testURI;
  }

  /**
   * Generate unique test path for multiple user tests.
   *
   * @return root test path
   */
  public static String generateUniqueTestPath() {
    String testUniqueForkId = System.getProperty("test.unique.fork.id");
    return testUniqueForkId == null ? "/test" :
        "/" + testUniqueForkId + "/test";
  }
}
