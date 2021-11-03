/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import io.daos.dfs.DaosUns;
import io.daos.dfs.uns.Layout;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.*;
import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.nio.file.Files;
import java.util.Random;
import java.util.concurrent.atomic.AtomicInteger;

/**
 *
 */
public class DaosFileSystemIT {

  private static FileSystem fs;

  private static AtomicInteger unsId = new AtomicInteger(1);

  @BeforeClass
  public static void setup() throws IOException {
    System.out.println("@BeforeClass");
    fs = DaosFSFactory.getFS();
  }

  @Test
  public void testServiceLoader() throws Exception {
    Configuration cfg = new Configuration();
    DaosFSFactory.config(cfg);
    FileSystem fileSystem = FileSystem.get(URI.create(DaosFSFactory.DAOS_URI), cfg);
    Assert.assertTrue(fileSystem instanceof DaosFileSystem);
  }

  @Test
  public void testNewDaosFileSystemFromUns() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    try {
      String path = file.getAbsolutePath();
      String daosAttr = String.format(io.daos.Constants.DUNS_XATTR_FMT, Layout.POSIX.name(),
          DaosFSFactory.getPooluuid(), DaosFSFactory.getContuuid());
      DaosUns.setAppInfo(path, io.daos.Constants.DUNS_XATTR_NAME, daosAttr);

      URI uri = URI.create("daos://" + io.daos.Constants.UNS_ID_PREFIX + unsId.getAndIncrement() + path);
      FileSystem fs = FileSystem.get(uri, new Configuration());
      Assert.assertNotNull(fs);
      fs.close();
      String path2 = "daos://" + io.daos.Constants.UNS_ID_PREFIX + unsId.getAndIncrement() + path;
      URI uri2 = URI.create(path2);
      FileSystem fs2 = FileSystem.get(uri2, new Configuration());
      Assert.assertNotEquals(fs, fs2);
      Assert.assertEquals(path, ((DaosFileSystem) fs2).getUnsPrefix());
      fs2.createNewFile(new Path(path2 + "111"));
      fs2.createNewFile(new Path(path2 + "/111"));
      fs2.close();
      // verify UNS path without authority
      URI uri3 = URI.create("daos://" + path);
      FileSystem fs3 = FileSystem.get(uri3, new Configuration(false));
      Assert.assertNotEquals(fs, fs3);
      Assert.assertEquals(path, ((DaosFileSystem) fs3).getUnsPrefix());
      fs3.createNewFile(new Path("daos://" + path + "/222"));
      fs3.close();
    } finally {
      file.delete();
    }
  }

  @Test
  public void testNewDaosFileSystemFromUnsWithLabels() throws Exception {
    String path = DaosFSFactory.getPoolLabel() + "/" + DaosFSFactory.getContLabel();
    String prefix = "/" + DaosFSFactory.getContLabel();
    URI uri = URI.create("daos://" + path);
    try (FileSystem fs = FileSystem.get(uri, new Configuration(false))) {
      Assert.assertNotNull(fs);
      DaosFileSystem dfs = (DaosFileSystem)fs;
      Assert.assertEquals(prefix, dfs.getUnsPrefix());
      // create and delete file with full path
      Path newFilePath = new Path(prefix + "/12345" + (new Random().nextInt(10000)));
      Assert.assertTrue(fs.createNewFile(newFilePath));
      fs.delete(newFilePath, true);
      // create and delete file with relative path
      newFilePath = new Path("/54321" + (new Random().nextInt(100)));
      Assert.assertTrue(fs.createNewFile(newFilePath));
      fs.delete(newFilePath, true);
    }
    // filesystem from long path
    String longPath = path + "/98765" +
        (new Random().nextInt(100));
    uri = URI.create("daos://" + longPath);
    try (FileSystem fs = FileSystem.get(uri, new Configuration(false))) {
      Assert.assertNotNull(fs);
      DaosFileSystem dfs = (DaosFileSystem)fs;
      Assert.assertEquals(prefix, dfs.getUnsPrefix());
      // create and delete file with full path
      Path newFilePath = new Path(prefix + "/12345" + (new Random().nextInt(10000)));
      Assert.assertTrue(fs.createNewFile(newFilePath));
      fs.delete(newFilePath, true);
      // create and delete file with relative path
      newFilePath = new Path("/54321" + (new Random().nextInt(100)));
      Assert.assertTrue(fs.createNewFile(newFilePath));
      fs.delete(newFilePath, true);
    }
  }

  @Test
  public void testNewDaosFileSystemFromUnsWithUUIDs() throws Exception {
    String path = DaosFSFactory.getPooluuid() + "/" + DaosFSFactory.getContuuid();
    String prefix = "/" + DaosFSFactory.getContuuid();
    URI uri = URI.create("daos://" + path);
    try (FileSystem fs = FileSystem.get(uri, new Configuration(false))) {
      Assert.assertNotNull(fs);
      DaosFileSystem dfs = (DaosFileSystem)fs;
      Assert.assertEquals(prefix, dfs.getUnsPrefix());
      // create and delete file with full path
      Path newFilePath = new Path(prefix + "/12345" + (new Random().nextInt(100)));
      Assert.assertTrue(fs.createNewFile(newFilePath));
      fs.delete(newFilePath, true);
      // create and delete file with relative path
      newFilePath = new Path("/54321" + (new Random().nextInt(100)));
      Assert.assertTrue(fs.createNewFile(newFilePath));
      fs.delete(newFilePath, true);
    }
    // filesystem from long path
    String suffix = "/98765" + (new Random().nextInt(100));
    String longPath = path + suffix;
    uri = URI.create("daos://" + longPath);
    try (FileSystem fs = FileSystem.get(uri, new Configuration(false))) {
      Assert.assertNotNull(fs);
      DaosFileSystem dfs = (DaosFileSystem)fs;
      Assert.assertEquals(prefix, dfs.getUnsPrefix());
      // create and delete file with full path
      Path newFilePath = new Path(prefix + "/12345" + (new Random().nextInt(100)));
      Assert.assertTrue(fs.createNewFile(newFilePath));
      fs.delete(newFilePath, true);
      // create and delete file with relative path
      newFilePath = new Path("/54321" + (new Random().nextInt(100)));
      Assert.assertTrue(fs.createNewFile(newFilePath));
      fs.delete(newFilePath, true);
    }
  }

  @Test
  public void testWriteReadAbsolutePathFromHybridUnsPath() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    try {
      String path = file.getAbsolutePath();
      String daosAttr = String.format(io.daos.Constants.DUNS_XATTR_FMT, Layout.POSIX.name(),
          DaosFSFactory.getPooluuid(), DaosFSFactory.getContuuid());
      DaosUns.setAppInfo(path, io.daos.Constants.DUNS_XATTR_NAME, daosAttr);
      String originPath = path;
      path += "/abc";
      String uriStr = "daos://" + io.daos.Constants.UNS_ID_PREFIX + unsId.getAndIncrement() + path;
      URI uri = URI.create(uriStr);
      FileSystem fs = FileSystem.get(uri, new Configuration());
      Assert.assertNotNull(fs);
      Assert.assertEquals(originPath, ((DaosFileSystem) fs).getUnsPrefix());
      // test create path with UNS prefix
      Path filePath = new Path(path, "123");
      String content = "qazwsxymv456";
      try (FSDataOutputStream fos = fs.create(filePath)) {
        fos.write(content.getBytes());
      }
      byte[] bytes = new byte[content.length()];
      try (FSDataInputStream fis = fs.open(filePath)) {
        fis.read(bytes);
      }
      Assert.assertEquals(content, new String(bytes));
      // work for path with schema, authority and UNS prefix.
      byte[] bytes2 = new byte[content.length()];
      filePath = new Path(uriStr + "/123");
      try (FSDataInputStream fis = fs.open(filePath)) {
        fis.read(bytes2);
      }
      Assert.assertEquals(content, new String(bytes2));
      // work for path without UNS prefix
      filePath = new Path("/def/789");
      try (FSDataOutputStream fos = fs.create(filePath)) {
        fos.write(content.getBytes());
      }
      byte[] bytes3 = new byte[content.length()];
      try (FSDataInputStream fis = fs.open(filePath)) {
        fis.read(bytes3);
      }
      Assert.assertEquals(content, new String(bytes3));
    } finally {
      file.delete();
    }
  }

  @Test
  public void testWriteReadRelativePathFromHybridUnsPath() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    try {
      String path = file.getAbsolutePath();
      String daosAttr = String.format(io.daos.Constants.DUNS_XATTR_FMT, Layout.POSIX.name(),
          DaosFSFactory.getPooluuid(), DaosFSFactory.getContuuid());
      DaosUns.setAppInfo(path, io.daos.Constants.DUNS_XATTR_NAME, daosAttr);
      String originPath = path;
      path += "/abc/def";
      String uriStr = "daos://" + io.daos.Constants.UNS_ID_PREFIX + unsId.getAndIncrement() + path;
      Path uriPath = new Path(uriStr);
      FileSystem fs = uriPath.getFileSystem(new Configuration());
      Assert.assertNotNull(fs);
      Assert.assertEquals(originPath, ((DaosFileSystem) fs).getUnsPrefix());
      // relative path
      Path filePath = new Path("xyz");
      String content = "qazwsxymv456";
      try (FSDataOutputStream fos = fs.create(filePath)) {
        fos.write(content.getBytes());
      }
      byte[] bytes = new byte[content.length()];
      try (FSDataInputStream fis = fs.open(filePath)) {
        fis.read(bytes);
      }
      Assert.assertEquals(content, new String(bytes));
    } finally {
      file.delete();
    }
  }

  @Test
  public void testConnectViaFileContext() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    try {
      String path = file.getAbsolutePath();
      String daosAttr = String.format(io.daos.Constants.DUNS_XATTR_FMT, Layout.POSIX.name(),
          DaosFSFactory.getPooluuid(), DaosFSFactory.getContuuid());
      DaosUns.setAppInfo(path, io.daos.Constants.DUNS_XATTR_NAME, daosAttr);
      String authority = io.daos.Constants.UNS_ID_PREFIX + unsId.getAndIncrement();
      String uriStr = "daos://" + authority + path;
      Configuration cfg = new Configuration();
      cfg.set("fs.defaultFS", uriStr);
      cfg.set("fs.AbstractFileSystem.daos.impl", "io.daos.fs.hadoop.DaosAbsFsImpl");
      AbstractFileSystem afs = FileContext.getFileContext(cfg).getDefaultFileSystem();
      Assert.assertEquals("daos://" + authority + "/", afs.getUri().toString());
      Assert.assertNotNull(afs);
    } finally {
      file.delete();
    }
  }

  @Test
  public void testConnectViaFileContextWithLabels() throws Exception {
    String uriStr = "daos://" + DaosFSFactory.getPoolLabel() + "/" + DaosFSFactory.getContLabel();
    Configuration cfg = new Configuration();
    cfg.set("fs.defaultFS", uriStr);
    cfg.set("fs.AbstractFileSystem.daos.impl", "io.daos.fs.hadoop.DaosAbsFsImpl");
    AbstractFileSystem afs = FileContext.getFileContext(cfg).getDefaultFileSystem();
    Assert.assertEquals("daos://" + DaosFSFactory.getPoolLabel() + "/", afs.getUri().toString());
    Assert.assertNotNull(afs);
  }

  @Test
  public void testDirAndListPathFromHybridUnsPath() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    try {
      String path = file.getAbsolutePath();
      String daosAttr = String.format(io.daos.Constants.DUNS_XATTR_FMT, Layout.POSIX.name(),
          DaosFSFactory.getPooluuid(), DaosFSFactory.getContuuid());
      DaosUns.setAppInfo(path, io.daos.Constants.DUNS_XATTR_NAME, daosAttr);
      String originPath = path;
      String relPath = "/hij/klm";
      path += relPath;
      String uri = "daos://" + io.daos.Constants.UNS_ID_PREFIX + unsId.getAndIncrement();
      String uriStr = uri + path;
      Path uriPath = new Path(uriStr);
      Configuration conf = new Configuration();
      conf.setBoolean(Constants.DAOS_WITH_UNS_PREFIX, false);
      FileSystem fs = uriPath.getFileSystem(conf);
      Assert.assertNotNull(fs);
      Assert.assertEquals(originPath, ((DaosFileSystem) fs).getUnsPrefix());
      fs.mkdirs(uriPath);
      for (int i = 0; i < 3; i++) {
        fs.mkdirs(new Path(uriStr, i + ""));
      }
      FileStatus children[] = fs.listStatus(uriPath);
      Assert.assertEquals(3, children.length);
      for (int i = 0; i < 3; i++) {
        Assert.assertEquals(uri + relPath + "/" + i, children[i].getPath().toString());
      }
    } finally {
      file.delete();
    }
  }

  @Test
  public void testListRootFromUnsPath() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    try {
      String path = file.getAbsolutePath();
      String daosAttr = String.format(io.daos.Constants.DUNS_XATTR_FMT, Layout.POSIX.name(),
          DaosFSFactory.getPooluuid(), DaosFSFactory.getContuuid());
      DaosUns.setAppInfo(path, io.daos.Constants.DUNS_XATTR_NAME, daosAttr);
      String originPath = path;
      path += "";
      String uri = "daos://" + io.daos.Constants.UNS_ID_PREFIX + unsId.getAndIncrement();
      String uriStr = uri + path;
      Path uriPath = new Path(uriStr);
      Configuration conf = new Configuration();
      conf.setBoolean(Constants.DAOS_WITH_UNS_PREFIX, false);
      FileSystem fs = uriPath.getFileSystem(conf);
      Assert.assertNotNull(fs);
      Assert.assertEquals(originPath, ((DaosFileSystem) fs).getUnsPrefix());

      FileStatus children[] = fs.listStatus(uriPath);
      Assert.assertTrue(children.length > 0);
      boolean found = false;
      for (int i = 0; i < children.length; i++) {
        if ((uri + "/user").equals(children[i].getPath().toString())) {
          found = true;
        }
      }
      Assert.assertTrue(found);
    } finally {
      file.delete();
    }
  }

  @AfterClass
  public static void teardown() throws Exception {
    if (fs != null) {
      fs.close();
    }
  }
}
