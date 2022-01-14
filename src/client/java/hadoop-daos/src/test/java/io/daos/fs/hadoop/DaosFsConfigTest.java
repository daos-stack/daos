/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import java.io.File;
import java.lang.reflect.Constructor;
import java.net.URL;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;

import org.apache.hadoop.conf.Configuration;
import org.junit.Assert;
import org.junit.Test;

public class DaosFsConfigTest {

  private DaosFsConfig config = DaosFsConfig.getInstance();
  private String help = config.getConfigHelp();

  @Test
  public void testFsConfigNamesSize() throws Exception {
    DaosFsConfig cf = DaosFsConfig.getInstance();
    Assert.assertEquals(12, cf.getFsConfigNames().size());
  }

  @Test
  public void testGetConfigItems() throws Exception {
    Assert.assertTrue(help.startsWith("-> DAOS URIs"));
    Assert.assertTrue(help.contains("-> DAOS FS Configs"));
    Assert.assertTrue(help.contains("-> DAOS FS Config to DAOS Container Examples"));
  }

  @Test
  public void testConfigItemExistence() throws Exception {
    for (String name : DaosFsConfig.getInstance().getFsConfigNames()) {
      Assert.assertTrue(Constants.DAOS_WITH_UNS_PREFIX.equals(name) || help.contains(name));
    }
  }

  @Test
  public void testParseConfigHadoopOverrideDaosContainer() throws Exception {
    Configuration hadoopConfig = new Configuration(false);
    hadoopConfig.set(Constants.DAOS_POOL_ID, "hadoop pid");
    hadoopConfig.set(Constants.DAOS_CONTAINER_ID, "hadoop cid");
    hadoopConfig.set(Constants.DAOS_READ_MINIMUM_SIZE, "9876");
    hadoopConfig.set(Constants.DAOS_CHUNK_SIZE, "45678");

    Map<String, String> daosConfig = new HashMap<>();
    daosConfig.put(Constants.DAOS_CHUNK_SIZE, "35678");
    daosConfig.put(Constants.DAOS_IO_ASYNC, "false");

    DaosFsConfig config = DaosFsConfig.getInstance();
    config.merge(null, hadoopConfig, daosConfig);
    Assert.assertEquals("hadoop pid", hadoopConfig.get(Constants.DAOS_POOL_ID, "not set"));
    Assert.assertEquals("hadoop cid", hadoopConfig.get(Constants.DAOS_CONTAINER_ID, "not set"));
    Assert.assertEquals("9876", hadoopConfig.get(Constants.DAOS_READ_MINIMUM_SIZE));
    Assert.assertEquals("45678", hadoopConfig.get(Constants.DAOS_CHUNK_SIZE));
    Assert.assertEquals("false", hadoopConfig.get(Constants.DAOS_IO_ASYNC));
  }

  @Test
  public void testParseConfigHadoopOverrideDefaultsByChoice() throws Exception {
    Configuration hadoopConfig = new Configuration(false);
    hadoopConfig.set(Constants.DAOS_POOL_ID, "hadoop pid");
    hadoopConfig.set(Constants.DAOS_CONTAINER_ID, "hadoop cid");
    hadoopConfig.set(Constants.DAOS_READ_MINIMUM_SIZE, "9876");
    hadoopConfig.set(Constants.DAOS_CHUNK_SIZE, "45678");

    Map<String, String> daosConfig = new HashMap<>();
    daosConfig.put(Constants.DAOS_CHUNK_SIZE, "35678");
    daosConfig.put(Constants.DAOS_POOL_ID, "daos pid");
    daosConfig.put(Constants.DAOS_IO_ASYNC, "false");

    daosConfig.put("spark." + Constants.DAOS_IO_ASYNC, "true");

    DaosFsConfig config = DaosFsConfig.getInstance();
    config.merge("spark", hadoopConfig, daosConfig);
    Assert.assertEquals("hadoop pid", hadoopConfig.get(Constants.DAOS_POOL_ID, "not set"));
    Assert.assertEquals("hadoop cid", hadoopConfig.get(Constants.DAOS_CONTAINER_ID, "not set"));
    Assert.assertEquals("9876", hadoopConfig.get(Constants.DAOS_READ_MINIMUM_SIZE));
    Assert.assertEquals("45678", hadoopConfig.get(Constants.DAOS_CHUNK_SIZE));
    Assert.assertEquals("true", hadoopConfig.get(Constants.DAOS_IO_ASYNC));
  }
}
