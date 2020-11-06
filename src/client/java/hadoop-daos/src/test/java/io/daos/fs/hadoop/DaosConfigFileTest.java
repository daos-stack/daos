/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package io.daos.fs.hadoop;

import java.io.File;
import java.lang.reflect.Constructor;
import java.net.URL;
import java.util.HashSet;
import java.util.Set;
import java.util.function.Consumer;

import org.apache.hadoop.conf.Configuration;
import org.junit.Assert;
import org.junit.Test;

public class DaosConfigFileTest {

  @Test
  public void testInstantiateWithoutDaosFile() throws Exception {
    URL url = DaosConfigFileTest.class.getResource("/daos-site.xml");
    if (url == null) {
      return;
    }
    File file = new File(url.getFile());
    File backFile = new File(file.getAbsolutePath() + ".back");
    try {
      if (!file.renameTo(backFile)) {
        throw new Exception("failed to rename daos file to " + backFile.getAbsolutePath());
      }
      Constructor<DaosConfigFile> constructor = DaosConfigFile.class.getDeclaredConstructor();
      constructor.setAccessible(true);
      DaosConfigFile config = constructor.newInstance();
      Assert.assertNull(config.getFromDaosFile(Constants.DAOS_DEFAULT_FS));
      Assert.assertEquals("unset", config.getFromDaosFile(Constants.DAOS_POOL_UUID, "unset"));
    } finally {
      if (!backFile.renameTo(file)) {
        throw new Exception("failed to rename daos file back");
      }
    }
  }

  @Test
  public void testFsConfigNamesSize() throws Exception {
    DaosConfigFile cf = DaosConfigFile.getInstance();
    Assert.assertEquals(10, cf.getFsConfigNames().size());
  }

  @Test
  public void testGetUriDesc() throws Exception {
    DaosConfigFile config = DaosConfigFile.getInstance();
    String desc = config.getDaosUriDesc().trim();
    Assert.assertTrue(desc.startsWith("Unique DAOS server"));
    Assert.assertTrue(desc.endsWith("Hadoop Configuration even URI has authority."));
    Assert.assertFalse(desc.contains("</description>"));
    Assert.assertTrue(desc.length() > 700);
  }

  @Test
  public void testParseConfigWithAllValuesDefault() throws Exception {
    Configuration hadoopConfig = new Configuration(false);
    Assert.assertNull(hadoopConfig.get(Constants.DAOS_DEFAULT_FS));
    Assert.assertNull(hadoopConfig.get(Constants.DAOS_POOL_UUID));
    Assert.assertNull(hadoopConfig.get(Constants.DAOS_CONTAINER_UUID));
    DaosConfigFile config = DaosConfigFile.getInstance();
    hadoopConfig = config.parseConfig(null, hadoopConfig);
    Assert.assertEquals("uuid of pool", hadoopConfig.get(Constants.DAOS_POOL_UUID));
    Assert.assertEquals("uuid of container", hadoopConfig.get(Constants.DAOS_CONTAINER_UUID));
  }

  @Test
  public void testParseConfigHadoopOverrideDefaultsWrong() throws Exception {
    Configuration hadoopConfig = new Configuration(false);
    hadoopConfig.set(Constants.DAOS_POOL_UUID, "hadoop pid");
    hadoopConfig.set(Constants.DAOS_CONTAINER_UUID, "hadoop cid");
    hadoopConfig.set(Constants.DAOS_READ_MINIMUM_SIZE, "9876");

    DaosConfigFile config = DaosConfigFile.getInstance();
    try {
      config.parseConfig("", hadoopConfig);
    } catch (Exception e) {
      Assert.assertTrue(e.getMessage().contains("hadoop pid"));
      Assert.assertTrue(e.getMessage().contains(config.getDaosUriDesc()));
    }
  }

  @Test
  public void testParseConfigHadoopOverrideDefaults() throws Exception {
    Configuration hadoopConfig = new Configuration(false);
    hadoopConfig.set(Constants.DAOS_POOL_UUID, "hadoop pid");
    hadoopConfig.set(Constants.DAOS_CONTAINER_UUID, "hadoop cid");
    hadoopConfig.set(Constants.DAOS_READ_MINIMUM_SIZE, "9876");
    hadoopConfig.set(Constants.DAOS_CHUNK_SIZE, "45678");

    DaosConfigFile config = DaosConfigFile.getInstance();
    hadoopConfig = config.parseConfig("pkeyc3", hadoopConfig);
    Assert.assertEquals("hadoop pid", hadoopConfig.get(Constants.DAOS_POOL_UUID));
    Assert.assertEquals("hadoop cid", hadoopConfig.get(Constants.DAOS_CONTAINER_UUID));
    Assert.assertEquals("9876", hadoopConfig.get(Constants.DAOS_READ_MINIMUM_SIZE));
    Assert.assertEquals("0", hadoopConfig.get(Constants.DAOS_POOL_SVC));
  }

  private void parseConfigWithDifferentDaosFile(String newDaosFile, Consumer<Constructor<DaosConfigFile>> function)
      throws Exception {
    URL url = DaosConfigFileTest.class.getResource("/daos-site.xml");
    if (url == null) {
      throw new Exception("cannot load daos-site.xml");
    }
    URL url2 = DaosConfigFileTest.class.getResource("/" + newDaosFile);
    if (url2 == null) {
      throw new Exception("cannot load " + newDaosFile);
    }
    File file = new File(url.getFile());
    File backFile = new File(file.getAbsolutePath() + ".back");

    File file2 = new File(url2.getFile());
    try {
      if (!file.renameTo(backFile)) {
        throw new Exception("failed to rename daos file to " + backFile.getAbsolutePath());
      }

      if (!file2.renameTo(file)) {
        throw new Exception("failed to rename test file to " + file.getAbsolutePath());
      }

      Constructor<DaosConfigFile> constructor = DaosConfigFile.class.getDeclaredConstructor();
      constructor.setAccessible(true);
      function.accept(constructor);
    } finally {
      if (!file.renameTo(file2)) {
        throw new Exception("failed to rename test file back");
      }
      if (!backFile.renameTo(file)) {
        throw new Exception("failed to rename daos file back");
      }
    }
  }

  private void poolDefaultFunction(Constructor<DaosConfigFile> constructor) {
    try {
      DaosConfigFile config = constructor.newInstance();
      Assert.assertNotNull(config.getFromDaosFile(Constants.DAOS_DEFAULT_FS));

      Configuration hadoopConfig = new Configuration(false);
      hadoopConfig = config.parseConfig("c2", hadoopConfig);
      Assert.assertEquals("uuid of pool", hadoopConfig.get(Constants.DAOS_POOL_UUID));
      Assert.assertEquals("c1 uuid", hadoopConfig.get(Constants.DAOS_CONTAINER_UUID));
      Assert.assertEquals("234567", hadoopConfig.get(Constants.DAOS_READ_BUFFER_SIZE));
      Assert.assertEquals("234567", hadoopConfig.get(Constants.DAOS_WRITE_BUFFER_SIZE));
      Assert.assertEquals("1234567", hadoopConfig.get(Constants.DAOS_BLOCK_SIZE));
      Assert.assertEquals("1048", hadoopConfig.get(Constants.DAOS_CHUNK_SIZE));
      Assert.assertEquals("-1", hadoopConfig.get(Constants.DAOS_READ_MINIMUM_SIZE));

      DaosConfigFile config2 = constructor.newInstance();
      Assert.assertNotNull(config2.getFromDaosFile(Constants.DAOS_DEFAULT_FS));

      Configuration hadoopConfig2 = new Configuration(false);
      hadoopConfig2.set(Constants.DAOS_READ_BUFFER_SIZE, "765432");
      hadoopConfig2.set(Constants.DAOS_CONTAINER_UUID, "hc1 uuid");
      hadoopConfig2.set(Constants.DAOS_READ_MINIMUM_SIZE, "24567");
      hadoopConfig2 = config2.parseConfig("c2", hadoopConfig2);
      Assert.assertEquals("uuid of pool", hadoopConfig2.get(Constants.DAOS_POOL_UUID));
      Assert.assertEquals("hc1 uuid", hadoopConfig2.get(Constants.DAOS_CONTAINER_UUID));
      Assert.assertEquals("765432", hadoopConfig2.get(Constants.DAOS_READ_BUFFER_SIZE));
      Assert.assertEquals("234567", hadoopConfig2.get(Constants.DAOS_WRITE_BUFFER_SIZE));
      Assert.assertEquals("1234567", hadoopConfig2.get(Constants.DAOS_BLOCK_SIZE));
      Assert.assertEquals("1048", hadoopConfig2.get(Constants.DAOS_CHUNK_SIZE));
      Assert.assertEquals("24567", hadoopConfig2.get(Constants.DAOS_READ_MINIMUM_SIZE));
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void testParseConfigWithPoolDefault() throws Exception {
    parseConfigWithDifferentDaosFile("daos-site-default-pool.xml", this::poolDefaultFunction);
  }

  private void containerDefaultFunction(Constructor<DaosConfigFile> constructor) {
    try {
      DaosConfigFile config = constructor.newInstance();
      Assert.assertNotNull(config.getFromDaosFile(Constants.DAOS_DEFAULT_FS));

      Configuration hadoopConfig = new Configuration(false);
      hadoopConfig = config.parseConfig("pool2", hadoopConfig);
      Assert.assertEquals("pool1 uuid", hadoopConfig.get(Constants.DAOS_POOL_UUID));
      Assert.assertEquals("uuid of container", hadoopConfig.get(Constants.DAOS_CONTAINER_UUID));
      Assert.assertEquals("234567", hadoopConfig.get(Constants.DAOS_READ_BUFFER_SIZE));
      Assert.assertEquals("234567", hadoopConfig.get(Constants.DAOS_WRITE_BUFFER_SIZE));
      Assert.assertEquals("1234567", hadoopConfig.get(Constants.DAOS_BLOCK_SIZE));
      Assert.assertEquals("1048", hadoopConfig.get(Constants.DAOS_CHUNK_SIZE));
      Assert.assertEquals("4194304", hadoopConfig.get(Constants.DAOS_READ_MINIMUM_SIZE));

      DaosConfigFile config2 = constructor.newInstance();
      Assert.assertNotNull(config2.getFromDaosFile(Constants.DAOS_DEFAULT_FS));

      Configuration hadoopConfig2 = new Configuration(false);
      hadoopConfig2.set(Constants.DAOS_READ_BUFFER_SIZE, "765432");
      hadoopConfig2.set(Constants.DAOS_POOL_UUID, "hp1 uuid");
      hadoopConfig2 = config2.parseConfig("pool2", hadoopConfig2);
      Assert.assertEquals("hp1 uuid", hadoopConfig2.get(Constants.DAOS_POOL_UUID));
      Assert.assertEquals("uuid of container", hadoopConfig2.get(Constants.DAOS_CONTAINER_UUID));
      Assert.assertEquals("765432", hadoopConfig2.get(Constants.DAOS_READ_BUFFER_SIZE));
      Assert.assertEquals("234567", hadoopConfig2.get(Constants.DAOS_WRITE_BUFFER_SIZE));
      Assert.assertEquals("1234567", hadoopConfig2.get(Constants.DAOS_BLOCK_SIZE));
      Assert.assertEquals("1048", hadoopConfig2.get(Constants.DAOS_CHUNK_SIZE));
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void testParseConfigWithContainerDefault() throws Exception {
    parseConfigWithDifferentDaosFile("daos-site-default-container.xml", this::containerDefaultFunction);
  }

  private void noDefaultFunction(Constructor<DaosConfigFile> constructor) {
    try {
      DaosConfigFile config = constructor.newInstance();
      Assert.assertNotNull(config.getFromDaosFile(Constants.DAOS_DEFAULT_FS));

      Configuration hadoopConfig = new Configuration(false);
      hadoopConfig = config.parseConfig("pool2c2", hadoopConfig);
      Assert.assertEquals("pool2 uuid", hadoopConfig.get(Constants.DAOS_POOL_UUID));
      Assert.assertEquals("c2 uuid", hadoopConfig.get(Constants.DAOS_CONTAINER_UUID));
      Assert.assertEquals("234567", hadoopConfig.get(Constants.DAOS_READ_BUFFER_SIZE));
      Assert.assertEquals("234567", hadoopConfig.get(Constants.DAOS_WRITE_BUFFER_SIZE));
      Assert.assertEquals("1234567", hadoopConfig.get(Constants.DAOS_BLOCK_SIZE));
      Assert.assertEquals("1048", hadoopConfig.get(Constants.DAOS_CHUNK_SIZE));

      DaosConfigFile config2 = constructor.newInstance();
      Assert.assertNotNull(config2.getFromDaosFile(Constants.DAOS_DEFAULT_FS));

      Configuration hadoopConfig2 = new Configuration(false);
      hadoopConfig2.set(Constants.DAOS_READ_BUFFER_SIZE, "765432");
      hadoopConfig2.set(Constants.DAOS_CHUNK_SIZE, "5698");
      hadoopConfig2.set(Constants.DAOS_POOL_UUID, "hp1 uuid");
      hadoopConfig2.set(Constants.DAOS_CONTAINER_UUID, "hc1 uuid");
      hadoopConfig2 = config2.parseConfig("pool2c2", hadoopConfig2);
      Assert.assertEquals("hp1 uuid", hadoopConfig2.get(Constants.DAOS_POOL_UUID));
      Assert.assertEquals("hc1 uuid", hadoopConfig2.get(Constants.DAOS_CONTAINER_UUID));
      Assert.assertEquals("765432", hadoopConfig2.get(Constants.DAOS_READ_BUFFER_SIZE));
      Assert.assertEquals("234567", hadoopConfig2.get(Constants.DAOS_WRITE_BUFFER_SIZE));
      Assert.assertEquals("1234567", hadoopConfig2.get(Constants.DAOS_BLOCK_SIZE));
      Assert.assertEquals("5698", hadoopConfig2.get(Constants.DAOS_CHUNK_SIZE));
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void testParseConfigWithNoDefault() throws Exception {
    parseConfigWithDifferentDaosFile("daos-site-no-default.xml", this::noDefaultFunction);
  }

  @Test
  public void testMergeWithExcludedAttribute() throws Exception {
    Constructor<DaosConfigFile> constructor = DaosConfigFile.class.getDeclaredConstructor();
    constructor.setAccessible(true);
    Configuration hconfig = new Configuration(false);
    DaosConfigFile config = constructor.newInstance();
    config.merge("fs2", hconfig, null);
    Assert.assertEquals("uuid of pool", hconfig.get(Constants.DAOS_POOL_UUID));
    Assert.assertEquals("uuid of container", hconfig.get(Constants.DAOS_CONTAINER_UUID));

    Set<String> exProps = new HashSet<>();
    exProps.add(Constants.DAOS_POOL_UUID);
    exProps.add(Constants.DAOS_CONTAINER_UUID);
    Configuration hconfig2 = new Configuration(false);
    hconfig2.set(Constants.DAOS_READ_BUFFER_SIZE, "765432");
    hconfig2.set(Constants.DAOS_CHUNK_SIZE, "5698");
    hconfig2.set(Constants.DAOS_POOL_UUID, "hp1 uuid");
    hconfig2.set(Constants.DAOS_CONTAINER_UUID, "hc1 uuid");
    config.merge("fs3", hconfig2, exProps);
    Assert.assertEquals("hp1 uuid", hconfig2.get(Constants.DAOS_POOL_UUID));
    Assert.assertEquals("hc1 uuid", hconfig2.get(Constants.DAOS_CONTAINER_UUID));
    Assert.assertEquals("5698", hconfig2.get(Constants.DAOS_CHUNK_SIZE));
    Assert.assertEquals("765432", hconfig2.get(Constants.DAOS_READ_BUFFER_SIZE));
  }
}
