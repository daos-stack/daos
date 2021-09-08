package io.daos.fs.hadoop;

import io.daos.dfs.DaosFile;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FSDataInputStream;
import org.apache.hadoop.fs.FSDataOutputStream;
import org.apache.hadoop.fs.Path;
import org.junit.Assert;
import org.junit.Test;
import org.powermock.reflect.Whitebox;

import java.net.URI;

public class DaosFileSystemAsyncIT {

  @Test
  public void testNewDaosFileSystem() throws Exception {
    DaosFileSystem daosFileSystem = new DaosFileSystem();
    Configuration cfg = new Configuration();
    DaosFSFactory.config(cfg, true);
    daosFileSystem.initialize(URI.create(DaosFSFactory.DAOS_URI), cfg);
    Assert.assertTrue(Whitebox.getInternalState(daosFileSystem, "async"));
    daosFileSystem.close();
  }

  @Test
  public void testOpen() throws Exception {
    DaosFileSystem daosFileSystem = new DaosFileSystem();
    Configuration cfg = new Configuration();
    DaosFSFactory.config(cfg, true);
    daosFileSystem.initialize(URI.create(DaosFSFactory.DAOS_URI), cfg);
    String path = "/async_1";
    DaosFile file = DaosFSFactory.getFsClient().getFile(path);
    file.createNewFile();
    file.release();

    FSDataInputStream ins = daosFileSystem.open(new Path(path));
    DaosInputStream dins = (DaosInputStream)ins.getWrappedStream();
    Assert.assertTrue(dins.getSource() instanceof DaosFileSourceAsync);
    dins.close();
    daosFileSystem.close();
  }

  @Test
  public void testCreate() throws Exception {
    DaosFileSystem daosFileSystem = new DaosFileSystem();
    Configuration cfg = new Configuration();
    DaosFSFactory.config(cfg, true);
    daosFileSystem.initialize(URI.create(DaosFSFactory.DAOS_URI), cfg);
    String path = "/async_2";

    FSDataOutputStream fos = daosFileSystem.create(new Path(path));
    DaosOutputStream dos = (DaosOutputStream)fos.getWrappedStream();
    Assert.assertTrue(dos.getSource() instanceof DaosFileSourceAsync);
    dos.close();
    daosFileSystem.close();
  }
}
