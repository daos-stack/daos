package io.daos.fs.hadoop;

import io.daos.dfs.DaosFsClient;
import org.apache.commons.io.FileUtils;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;
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
import java.io.InputStream;
import java.net.URI;

import static org.mockito.Mockito.*;

@RunWith(PowerMockRunner.class)
@PowerMockIgnore("javax.management.*")
@PrepareForTest({DaosFsClient.DaosFsClientBuilder.class, DaosFileSystem.class})
@SuppressStaticInitializationFor("io.daos.dfs.DaosFsClient")
public class DaosFileSystemTest {

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

    FileSystem fs1 = FileSystem.get(URI.create("daos://1234:56/"), cfg);
    FileSystem fs2 = FileSystem.get(URI.create("daos://12345:567/"), cfg);
    Assert.assertNotSame(fs1, fs2);
    fs1.close();
    fs2.close();
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
    Assert.assertEquals("daos://1234:56/user/"+System.getProperty("user.name"), fs.getWorkingDirectory().toString());
    verify(client, times(1)).mkdir("/user/"+System.getProperty("user.name"), true);
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
      fs.initialize(URI.create("daos://1234:56/root"), cfg);
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
      fs.initialize(URI.create("daos://1234:56/root"), cfg);
    } catch (IllegalArgumentException e) {
      Assert.assertTrue(e.getMessage().contains(Constants.DAOS_CONTAINER_UUID));
      throw e;
    } finally {
      fs.close();
    }
  }

  @Test(expected = IllegalArgumentException.class)
  public void testNewDaosFileSystemFailedNoSvc() throws Exception {
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
      fs.initialize(URI.create("daos://1234:56/root"), cfg);
    } catch (IllegalArgumentException e) {
      Assert.assertTrue(e.getMessage().contains(Constants.DAOS_POOL_SVC));
      throw e;
    } finally {
      fs.close();
    }
  }

  @Test
  public void testBufferedReadConfigurationKey() throws Exception {
    PowerMockito.mockStatic(DaosFsClient.class);
    DaosFsClient.DaosFsClientBuilder builder = mock(DaosFsClient.DaosFsClientBuilder.class);
    DaosFsClient client = mock(DaosFsClient.class);
    Configuration conf = new Configuration();

    PowerMockito.whenNew(DaosFsClient.DaosFsClientBuilder.class).withNoArguments().thenReturn(builder);
    when(builder.poolId(anyString())).thenReturn(builder);
    when(builder.containerId(anyString())).thenReturn(builder);
    when(builder.ranks(anyString())).thenReturn(builder);
    when(builder.build()).thenReturn(client);
    conf.set(Constants.DAOS_POOL_UUID, "123");
    conf.set(Constants.DAOS_CONTAINER_UUID, "123");
    conf.set(Constants.DAOS_POOL_SVC, "0");

    DaosFileSystem fs = new DaosFileSystem();
    fs.initialize(URI.create("daos://1234:56"), conf);
    Assert.assertTrue(fs.isPreloadEnabled());
    fs.close();
    // if not set, should be default
    conf.setInt(Constants.DAOS_PRELOAD_SIZE, 0);
    fs.initialize(URI.create("daos://1234:56"), conf);
    Assert.assertFalse(fs.isPreloadEnabled());
    fs.close();
  }

  @Test
  public void testLoadingConfig() throws Exception {
    Configuration cfg = new Configuration(false);
    cfg.addResource("daos-site.xml");
    String s = cfg.get("fs.defaultFS");
    Assert.assertEquals("daos://default:1", s);
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
    Assert.assertEquals("daos://default:1", s);
    Assert.assertEquals(8388608, cfg.getInt("fs.daos.read.buffer.size", 0));
  }

  @Test
  public void testLoadingConfigFromCoreSite() throws Exception {
    Configuration cfg = new Configuration(true);
    String s = cfg.get("fs.defaultFS");
    Assert.assertEquals("daos://id:2", s);
    Assert.assertEquals(8388608, cfg.getInt("fs.daos.read.buffer.size", 0));
  }
}
