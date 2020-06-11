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

package io.daos.fs.hadoop;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.net.URI;
import java.util.Arrays;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.FsShell;
import org.apache.hadoop.util.ToolRunner;
import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

public class HadoopCmdIT {

  private static Configuration conf;

  @BeforeClass
  public static void setup() throws Exception {
    conf = new Configuration();
    conf.set(Constants.DAOS_POOL_UUID, DaosFSFactory.pooluuid);
    conf.set(Constants.DAOS_CONTAINER_UUID, DaosFSFactory.contuuid);
    FileSystem.get(new URI((conf.get("fs.defaultFS"))), conf);
  }

  private int run(String argv[]) throws Exception {
    FsShell shell = new FsShell();
    conf.setQuietMode(false);
    shell.setConf(conf);
    int res;
    System.out.println("main argv: " + Arrays.asList(argv));
    try {
      res = ToolRunner.run(shell, argv);
      return res;
    } finally {
      //no close since FS is shared
//      shell.close();
    }
  }

  @Test
  public void listRoot() throws Exception {
    ByteArrayOutputStream bos = new ByteArrayOutputStream();
    PrintStream ps = null;
    PrintStream defaultPs = System.out;
    try {
      ps = new PrintStream(bos);
      System.setOut(ps);

      String[] argv = new String[]{"-ls", "/"};
      // FsShell.main(args) cannot be used since it calls System.exit
      int res = run(argv);
      Assert.assertTrue(res == 0);
      ps.flush();
      Assert.assertTrue(new String(bos.toByteArray()).indexOf("/user") > 0);
    } finally {
      System.setOut(defaultPs);
      ps.close();
    }
  }

  @Test
  public void testMkdir() throws Exception {
    String filePath = "/job_1581472776049_0003-1581473346405-" +
            "root-autogen%2D7.1%2DSNAPSHOT%2Djar%2Dwith%2Ddependencies.jar-" +
            "1581473454525-16-1-SUCCEEDED-default-1581473439146.jhist_tmp";
    String[] argv = new String[]{"-rm", "-r", filePath};
    int res = run(argv);
//    Assert.assertTrue(res == 0);

    argv = new String[]{"-mkdir", filePath};
    res = run(argv);
    Assert.assertTrue(res == 0);
  }

  @AfterClass
  public static void teardown() {
    //DO NOT CLOSE fs SINCE IT'S SHARED
  }
}
