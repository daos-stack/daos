/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
    FileSystem.get(new URI(DaosFSFactory.DAOS_URI), conf);
    conf.set(Constants.DAOS_DEFAULT_FS, DaosFSFactory.DAOS_URI);
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
  public void testMkdirSpecialLongPath() throws Exception {
    String filePath = DaosFSFactory.DAOS_URI +"/zjf/job_1581472776049_0003-1581473346405-" +
        "root-autogen%2D7.1%2DSNAPSHOT%2Djar%2Dwith%2Ddependencies.jar-" +
        "1581473454525-16-1-SUCCEEDED-default-1581473439146.jhist_tmp";
    String[] argv = new String[]{"-rm", "-r", filePath};
    run(argv);

    String[] argv2 = new String[]{"-mkdir", "-p", filePath};
    int res = run(argv2);
    Assert.assertTrue(res == 0);

    res = run(argv);
    Assert.assertTrue(res == 0);
  }

  @Test
  public void testMkdirSpecialEqualAndComma() throws Exception {
    String filePath = DaosFSFactory.DAOS_URI +"/zjf/useDecimal=true,xyz=1";
    String filePath2 = DaosFSFactory.DAOS_URI +"/zjf/normal";
    String[] argv = new String[]{"-rm", "-r", filePath};
    run(argv);

    String[] argv0 = new String[]{"-rm", "-r", filePath2};
    run(argv0);

    String[] argv2 = new String[]{"-mkdir", "-p", filePath};
    int res = run(argv2);
    Assert.assertTrue(res == 0);

    String[] argv20 = new String[]{"-mkdir", "-p", filePath2};
    res = run(argv20);
    Assert.assertTrue(res == 0);

    String[] argv3 = new String[]{"-ls", DaosFSFactory.DAOS_URI +"/zjf"};
    res = run(argv3);
    Assert.assertTrue(res == 0);
    res = run(argv);
    Assert.assertTrue(res == 0);
  }

  @AfterClass
  public static void teardown() {
    //DO NOT CLOSE fs SINCE IT'S SHARED
  }
}
