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

import io.daos.dfs.DaosUns;
import io.daos.dfs.uns.Layout;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.*;
import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

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
  private static final Logger LOG = LoggerFactory.getLogger(DaosFileSystemIT.class);

  private static FileSystem fs;

  private static AtomicInteger unsId = new AtomicInteger(1);

  @BeforeClass
  public static void setup() throws IOException {
    System.out.println("@BeforeClass");
    fs = DaosFSFactory.getFS();
  }

  //every time test one
  @Test
  public void testInitialization() throws Exception {
    initializationTest("daos:///", "daos:///");
    initializationTest("daos://192.168.2.1:2345/", "daos://192.168.2.1:2345");
    initializationTest("daos://192.168.2.1:2345/abc", "daos://192.168.2.1:2345");
    initializationTest("daos://192.168.2.1:2345/ae/", "daos://192.168.2.1:2345");
    initializationTest("daos://192.168.2.1:2345/ac/path", "daos://192.168.2.1:2345");
    initializationTest("daos://192.168.2.1:2345/ac", "daos://192.168.2.1:2345");
    initializationTest("daos://192.168.2.1:2345/ad_c/", "daos://192.168.2.1:2345");
    initializationTest("daos://192.168.2.1:2345/ac2/path", "daos://192.168.2.1:2345");
    initializationTest("daos://192.168.2.1:2345/c.3", "daos://192.168.2.1:2345");
    initializationTest("daos://192.168.2.1:2345/234/", "daos://192.168.2.1:2345");
  }

  @Test
  public void testServiceLoader() throws Exception {
    Configuration cfg = new Configuration();
    cfg.set(Constants.DAOS_POOL_UUID, DaosFSFactory.pooluuid);
    cfg.set(Constants.DAOS_CONTAINER_UUID, DaosFSFactory.contuuid);
    cfg.set(Constants.DAOS_POOL_SVC, DaosFSFactory.svc);
    FileSystem fileSystem = FileSystem.get(URI.create("daos://2345:567/"), cfg);
    Assert.assertTrue(fileSystem instanceof DaosFileSystem);
  }

  private void initializationTest(String initializationUri, String expectedUri) throws Exception {
    fs.initialize(URI.create(initializationUri), DaosHadoopTestUtils.getConfiguration());
    Assert.assertEquals(URI.create(expectedUri), fs.getUri());
  }

  @Test
  public void testNewDaosFileSystemFromUns() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    try {
      String path = file.getAbsolutePath();
      String daosAttr = String.format(io.daos.Constants.DUNS_XATTR_FMT, Layout.POSIX.name(),
          DaosFSFactory.getPooluuid(), DaosFSFactory.getContuuid());
      DaosUns.setAppInfo(path, io.daos.Constants.DUNS_XATTR_NAME, daosAttr);
      DaosUns.setAppInfo(path, Constants.UNS_ATTR_NAME_HADOOP,
          Constants.DAOS_POOL_FLAGS + "=2:");

      URI uri = URI.create("daos://" + unsId.getAndIncrement() + path);
      FileSystem fs = FileSystem.get(uri, new Configuration());
      Assert.assertNotNull(fs);
      fs.close();
      URI uri2 = URI.create("daos://" + unsId.getAndIncrement() + path);
      FileSystem fs2 = FileSystem.get(uri2, new Configuration());
      Assert.assertNotEquals(fs, fs2);
      Assert.assertEquals(path, ((DaosFileSystem) fs2).getUnsPrefix());
      fs2.close();
      // verify UNS path without authority
      URI uri3 = URI.create("daos:///" + path);
      FileSystem fs3 = FileSystem.get(uri3, new Configuration(false));
      Assert.assertNotEquals(fs, fs3);
      Assert.assertEquals(path, ((DaosFileSystem) fs3).getUnsPrefix());
      Assert.assertEquals("8388608", fs3.getConf().get(Constants.DAOS_READ_BUFFER_SIZE));
      fs3.close();
    } finally {
      file.delete();
    }
  }

  @Test
  public void testNewDaosFileSystemFromUnsWithUUIDs() throws Exception {
      String path = "/" + DaosFSFactory.getPooluuid() + "/" + DaosFSFactory.getContuuid();
      URI uri = URI.create("daos://" + path);
      try (FileSystem fs = FileSystem.get(uri, new Configuration(false))) {
        Assert.assertNotNull(fs);
        DaosFileSystem dfs = (DaosFileSystem)fs;
        Assert.assertEquals(path, dfs.getUnsPrefix());
        // create and delete file with full path
        Path newFilePath = new Path(path + "/12345" + (new Random().nextInt(100)));
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
        Assert.assertEquals(path, dfs.getUnsPrefix());
        // create and delete file with full path
        Path newFilePath = new Path(longPath + "/12345" + (new Random().nextInt(100)));
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
      String uriStr = "daos://" + unsId.getAndIncrement() +
          path;
      URI uri = URI.create(uriStr);
      FileSystem fs = FileSystem.get(uri, new Configuration());
      Assert.assertNotNull(fs);
      Assert.assertTrue(((DaosFileSystem) fs).isUns());
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
      String uriStr = "daos://" + ":" + unsId.getAndIncrement() +
          path;
      Path uriPath = new Path(uriStr);
      FileSystem fs = uriPath.getFileSystem(new Configuration());
      Assert.assertNotNull(fs);
      Assert.assertTrue(((DaosFileSystem) fs).isUns());
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
  public void testDirAndListPathFromHybridUnsPath() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    try {
      String path = file.getAbsolutePath();
      String daosAttr = String.format(io.daos.Constants.DUNS_XATTR_FMT, Layout.POSIX.name(),
          DaosFSFactory.getPooluuid(), DaosFSFactory.getContuuid());
      DaosUns.setAppInfo(path, io.daos.Constants.DUNS_XATTR_NAME, daosAttr);
      String originPath = path;
      path += "/hij/klm";
      String uriStr = "daos://" + ":" + unsId.getAndIncrement() +
          path;
      Path uriPath = new Path(uriStr);
      FileSystem fs = uriPath.getFileSystem(new Configuration());
      Assert.assertNotNull(fs);
      Assert.assertTrue(((DaosFileSystem) fs).isUns());
      Assert.assertEquals(originPath, ((DaosFileSystem) fs).getUnsPrefix());
      fs.mkdirs(uriPath);
      for (int i = 0; i < 3; i++) {
        fs.mkdirs(new Path(uriStr, i + ""));
      }
      FileStatus children[] = fs.listStatus(uriPath);
      Assert.assertEquals(3, children.length);
      for (int i = 0; i < 3; i++) {
        Assert.assertEquals(uriStr + "/" + i, children[i].getPath().toString());
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
      String uriStr = "daos://" + ":" + unsId.getAndIncrement() +
          path;
      Path uriPath = new Path(uriStr);
      FileSystem fs = uriPath.getFileSystem(new Configuration());
      Assert.assertNotNull(fs);
      Assert.assertTrue(((DaosFileSystem) fs).isUns());
      Assert.assertEquals(originPath, ((DaosFileSystem) fs).getUnsPrefix());

      FileStatus children[] = fs.listStatus(uriPath);
      Assert.assertTrue(children.length > 0);
      boolean found = false;
      for (int i = 0; i < children.length; i++) {
        if ((uriStr + "/user").equals(children[i].getPath().toString())) {
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
