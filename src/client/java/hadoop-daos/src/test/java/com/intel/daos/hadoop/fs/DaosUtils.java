/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.intel.daos.hadoop.fs;

import org.apache.commons.lang.StringUtils;
import org.apache.hadoop.conf.Configuration;
import org.junit.internal.AssumptionViolatedException;

import java.io.IOException;
import java.net.URI;

/**
 *
 */
public class DaosUtils {
  private static Configuration configuration;
  public static final String TEST_FS_DAOS_NAME = "test.fs.daos.name";

  private DaosUtils(){}

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
            DaosUtils.TEST_FS_DAOS_NAME, "daos://192.168.2.1:23456/");

    boolean liveTest = !StringUtils.isEmpty(fsname);
    URI testURI = null;
    if (liveTest) {
      testURI = URI.create(fsname);
      liveTest = testURI.getScheme().equals(Constants.DAOS_SCHEMA);
    }

    if (!liveTest) {
      throw new AssumptionViolatedException("No test filesystem in "
          + DaosUtils.TEST_FS_DAOS_NAME);
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
