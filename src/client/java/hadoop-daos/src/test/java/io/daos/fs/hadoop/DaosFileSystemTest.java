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

import io.daos.dfs.DaosFsClient;
import io.daos.dfs.DaosUns;
import io.daos.DaosUtils;
import io.daos.dfs.DunsInfo;
import org.apache.commons.io.FileUtils;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.security.UserGroupInformation;
import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.powermock.api.mockito.PowerMockito;
import org.powermock.core.classloader.annotations.PowerMockIgnore;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.core.classloader.annotations.SuppressStaticInitializationFor;
import org.powermock.modules.junit4.PowerMockRunner;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.util.concurrent.atomic.AtomicInteger;

import static org.mockito.Mockito.*;

@RunWith(PowerMockRunner.class)
@PowerMockIgnore("javax.management.*")
@PrepareForTest({DaosFsClient.DaosFsClientBuilder.class, DaosFileSystem.class, DaosUns.class})
@SuppressStaticInitializationFor({"io.daos.dfs.DaosFsClient", "io.daos.DaosClient"})
public class DaosFileSystemTest {

  private static AtomicInteger unsId = new AtomicInteger(1);

  @Test
  public void testNewDaosFileSystemByDifferentURIs() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);

    Configuration cfg = new Configuration();
    cfg.set(Constants.DAOS_POOL_UUID, "123");
    cfg.set(Constants.DAOS_CONTAINER_UUID, "56");
    cfg.set(Constants.DAOS_POOL_SVC, "0");

    DaosFsClient client = mock(DaosFsClient.class);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.ranks(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    UserGroupInformation.setLoginUser(UserGroupInformation.createRemoteUser("test"));

    FileSystem fs1 = FileSystem.get(URI.create("daos://fs1/"), cfg);
    FileSystem fs2 = FileSystem.get(URI.create("daos://12345:567/"), cfg);
    Assert.assertNotSame(fs1, fs2);
    fs1.close();
    fs2.close();

    FileSystem fs3 = FileSystem.get(URI.create("daos:///"), cfg);
    Assert.assertNotNull(fs3);
    fs3.close();

    String path = "/file/abc";
    DunsInfo info = new DunsInfo("123", "56", "POSIX", Constants.DAOS_POOL_SVC + "=0",
        path);
    PowerMockito.mockStatic(DaosUns.class);
    when(DaosUns.getAccessInfo(anyString(), eq(Constants.UNS_ATTR_NAME_HADOOP),
        eq(io.daos.Constants.UNS_ATTR_VALUE_MAX_LEN_DEFAULT), eq(false))).thenReturn(info);
    URI uri = URI.create("daos://" + unsId.getAndIncrement() + path);
    FileSystem unsFs = FileSystem.get(uri, cfg);
    unsFs.close();

    IOException eie = null;
    try {
      FileSystem.get(URI.create("daosss://file/abc"), cfg);
    } catch (IOException e) {
      eie = e;
    }
    Assert.assertNotNull(eie);
    Assert.assertTrue(eie.getMessage().contains("No FileSystem for scheme: daosss"));
  }

  @Test
  public void testNewDaosFileSystemFromUnsWithAppInfo() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);

    Configuration cfg = new Configuration();
    DaosFsClient client = mock(DaosFsClient.class);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.ranks(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    UserGroupInformation.setLoginUser(UserGroupInformation.createRemoteUser("test"));

    String path = "/file/abc";
    StringBuilder sb = new StringBuilder();
    sb.append(Constants.DAOS_SERVER_GROUP).append("=").append(DaosUtils.escapeUnsValue("daos_=:group")).append(":");
    sb.append(Constants.DAOS_POOL_UUID).append("=").append("456").append(":");
    sb.append(Constants.DAOS_CONTAINER_UUID).append("=").append("789").append(":");
    sb.append(Constants.DAOS_POOL_SVC).append("=").append(DaosUtils.escapeUnsValue("0,1:2,3")).append(":");
    sb.append(Constants.DAOS_POOL_FLAGS).append("=").append("4").append(":");
    sb.append(Constants.DAOS_READ_BUFFER_SIZE).append("=").append("4194304").append(":");
    DunsInfo info = new DunsInfo("123", "56", "POSIX",
        sb.toString(), path);
    PowerMockito.mockStatic(DaosUns.class);
    when(DaosUns.getAccessInfo(anyString(), eq(Constants.UNS_ATTR_NAME_HADOOP),
        eq(io.daos.Constants.UNS_ATTR_VALUE_MAX_LEN_DEFAULT), eq(false))).thenReturn(info);
    URI uri = URI.create("daos://" + unsId.getAndIncrement() + path);
    FileSystem unsFs = FileSystem.get(uri, cfg);
    unsFs.close();

    Assert.assertEquals("123", cfg.get(Constants.DAOS_POOL_UUID));
    Assert.assertEquals("56", cfg.get(Constants.DAOS_CONTAINER_UUID));
    Assert.assertEquals("daos_=:group", cfg.get(Constants.DAOS_SERVER_GROUP));
    Assert.assertEquals("8388608", cfg.get(Constants.DAOS_READ_BUFFER_SIZE));
    Assert.assertEquals("0", cfg.get(Constants.DAOS_POOL_SVC));
  }

  @Test
  public void testNewDaosFileSystemSuccessfulAndCreateRootDir() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    DaosFsClient client = mock(DaosFsClient.class);

    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.ranks(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    DaosFileSystem fs = new DaosFileSystem();
    Configuration cfg = new Configuration();
    cfg.set(Constants.DAOS_POOL_UUID, "123");
    cfg.set(Constants.DAOS_CONTAINER_UUID, "123");
    cfg.set(Constants.DAOS_POOL_SVC, "0");
    fs.initialize(URI.create("daos://1234:56/"), cfg);
    Assert.assertEquals("daos://1234:56/user/" + System.getProperty("user.name"),
        fs.getWorkingDirectory().toString());
    verify(client, times(1))
        .mkdir("/user/" + System.getProperty("user.name"), true);
    fs.close();
  }

  @Test(expected = IllegalArgumentException.class)
  public void testNewDaosFileSystemFailedNoPoolId() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    DaosFsClient client = mock(DaosFsClient.class);

    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.ranks(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    DaosFileSystem fs = new DaosFileSystem();
    Configuration cfg = new Configuration();
    cfg.set(Constants.DAOS_POOL_UUID, "");
    cfg.set(Constants.DAOS_CONTAINER_UUID, "123");
    cfg.set(Constants.DAOS_POOL_SVC, "0");
    try {
      fs.initialize(URI.create("daos://1234:56"), cfg);
    } catch (IllegalArgumentException e) {
      Assert.assertTrue(e.getMessage().contains(Constants.DAOS_POOL_UUID));
      throw e;
    } finally {
      fs.close();
    }
  }

  @Test(expected = IllegalArgumentException.class)
  public void testNewDaosFileSystemFailedNoContId() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    DaosFsClient client = mock(DaosFsClient.class);

    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.ranks(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    DaosFileSystem fs = new DaosFileSystem();
    Configuration cfg = new Configuration();
    cfg.set(Constants.DAOS_POOL_UUID, "123");
    cfg.set(Constants.DAOS_CONTAINER_UUID, "");
    cfg.set(Constants.DAOS_POOL_SVC, "0");
    try {
      fs.initialize(URI.create("daos://1234:56/"), cfg);
    } catch (IllegalArgumentException e) {
      Assert.assertTrue(e.getMessage().contains(Constants.DAOS_CONTAINER_UUID));
      throw e;
    } finally {
      fs.close();
    }
  }

  @Test
  public void testNewDaosFileSystemSuccessfulNoSvc() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    DaosFsClient client = mock(DaosFsClient.class);

    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.ranks(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    DaosFileSystem fs = new DaosFileSystem();
    Configuration cfg = new Configuration();
    cfg.set(Constants.DAOS_POOL_UUID, "123");
    cfg.set(Constants.DAOS_CONTAINER_UUID, "123");
    cfg.set(Constants.DAOS_POOL_SVC, "");
    try {
      URI uri = URI.create("daos://1234:56/");
      fs.initialize(uri, cfg);
      Assert.assertEquals(new Path("/user", System.getProperty("user.name")).makeQualified(uri, null),
          fs.getWorkingDirectory());
    } finally {
      fs.close();
    }
  }

  @Test
  public void testLoadingConfig() throws Exception {
    Configuration cfg = new Configuration(false);
    cfg.addResource("daos-site.xml");
    String s = cfg.get("fs.defaultFS");
    Assert.assertEquals("daos:///", s);
    Assert.assertEquals(8388608, cfg.getInt("fs.daos.read.buffer.size", 0));
  }

  @Test
  public void testLoadingConfigFromStream() throws Exception {
    Configuration cfg = new Configuration(false);

    File tempFile = File.createTempFile("daos", "");
    try (InputStream is = this.getClass().getResourceAsStream("/daos-site.xml")) {
      FileUtils.copyInputStreamToFile(is, tempFile);
    }
    cfg.addResource(tempFile.toURI().toURL(), false);
    String s = cfg.get("fs.defaultFS");
    Assert.assertEquals("daos:///", s);
    Assert.assertEquals(8388608, cfg.getInt("fs.daos.read.buffer.size", 0));
  }

  @Test
  public void testLoadingConfigFromCoreSite() throws Exception {
    Configuration cfg = new Configuration(true);
    String s = cfg.get("fs.defaultFS");
    Assert.assertEquals("daos://id:2/", s);
    Assert.assertEquals(8388608, cfg.getInt("fs.daos.read.buffer.size", 0));
  }

  @Test
  public void testSpecialUnsPath() throws Exception {
    URI uri = URI.create("daos://uns:" + unsId.getAndIncrement() + "/tmp/uns_path#/abc");
    Assert.assertEquals("/tmp/uns_path", uri.getPath());
    Assert.assertEquals("/abc", uri.getFragment());

    Path path = new Path("daos://uns:" + unsId.getAndIncrement() + "/tmp/uns_path#abc");
    Path path1 = path.makeQualified(uri, null);
    System.out.println(path1);


    Assert.assertEquals("/tmp/uns_path#abc", path.toUri().getPath());
    Assert.assertEquals(null, path.toUri().getFragment());

    Path path2 = new Path("/abc");
    System.out.println(path2.toString());
    Path path3 = path2.makeQualified(uri, new Path("/user/zjf"));
    System.out.println(path3.toString());

    Path path4 = new Path("abc");
    System.out.println(path4.toUri().getPath());

    String s = "daos://uns:" + unsId.getAndIncrement() + "/tmp/uns_path#";
    Path p6 = new Path(s + path2.toString());
    System.out.println(p6);
  }
}
