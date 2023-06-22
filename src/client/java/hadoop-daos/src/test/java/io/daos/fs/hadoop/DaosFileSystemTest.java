/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import io.daos.Constants;
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
  public void testNewDaosFileSystemByUnsWithAuthority() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);

    Configuration cfg = new Configuration();

    DaosFsClient client = mock(DaosFsClient.class);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    UserGroupInformation.setLoginUser(UserGroupInformation.createRemoteUser("test"));

    String path = "/file/abc";
    DunsInfo info = new DunsInfo("123", "56", "POSIX", path);
    PowerMockito.mockStatic(DaosUns.class);
    when(DaosUns.getAccessInfo(any(URI.class))).thenReturn(info);
    URI uri = URI.create("daos://" + Constants.UNS_ID_PREFIX + "-" + unsId.getAndIncrement() + path);
    FileSystem unsFs = FileSystem.get(uri, cfg);
    unsFs.close();

    IOException eie = null;
    try {
      FileSystem.get(URI.create("daosss://file/abc"), cfg);
    } catch (IOException e) {
      eie = e;
    }
    Assert.assertNotNull(eie);
    Assert.assertTrue(eie.getMessage().contains("No FileSystem for scheme \"daosss\""));
  }

  @Test
  public void testNewDaosFileSystemSuccessfulAndCreateRootDir() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    DaosFsClient client = mock(DaosFsClient.class);

    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    DaosFileSystem fs = new DaosFileSystem();
    Configuration cfg = new Configuration();

    DunsInfo info = new DunsInfo("123", "123", "POSIX", "/123");
    PowerMockito.mockStatic(DaosUns.class);
    when(DaosUns.getAccessInfo(any(URI.class))).thenReturn(info);
    cfg.setBoolean(io.daos.fs.hadoop.Constants.DAOS_WITH_UNS_PREFIX, true);
    fs.initialize(URI.create("daos://123/123/abc"), cfg);
    Assert.assertEquals("daos://123/123/user/" + System.getProperty("user.name"),
        fs.getWorkingDirectory().toString());
    verify(client, times(1))
        .mkdir("/user/" + System.getProperty("user.name"), true);
    fs.close();
  }

  @Test(expected = IllegalArgumentException.class)
  public void testNewDaosFileSystemByBadURIAuthority() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    DaosFsClient client = mock(DaosFsClient.class);

    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    DaosFileSystem fs = new DaosFileSystem();
    Configuration cfg = new Configuration();
    try {
      fs.initialize(URI.create("daos://1234:56"), cfg);
    } finally {
      fs.close();
    }
  }

  @Test(expected = IllegalArgumentException.class)
  public void testNewDaosFileSystemByBadURINoAuthority() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    DaosFsClient client = mock(DaosFsClient.class);

    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);

    DaosFileSystem fs = new DaosFileSystem();
    Configuration cfg = new Configuration();
    try {
      fs.initialize(URI.create("daos:///"), cfg);
    } finally {
      fs.close();
    }
  }

  @Test
  public void testLoadingConfigFromCoreSite() throws Exception {
    Configuration cfg = new Configuration(true);
    String s = cfg.get("fs.defaultFS");
    Assert.assertEquals("daos:///", s);
    Assert.assertEquals(8388608, cfg.getInt("fs.daos.read.buffer.size", 0));
  }
}
