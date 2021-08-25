/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop.multiple;

import io.daos.dfs.DaosUns;
import io.daos.dfs.uns.Layout;
import io.daos.fs.hadoop.DaosFSFactory;
import io.daos.fs.hadoop.DaosFileSystem;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FSDataOutputStream;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.permission.FsPermission;
import org.junit.Assert;
import org.junit.Test;

import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.nio.file.Files;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class ConcurrentFileOperationIT {

  class FileOperation implements  Runnable {
    String path;
    AtomicInteger counter;
    FileSystem fs;
    int opType;
    FileOperation(String path, AtomicInteger counter, int opType) {
      this.path = path;
      this.counter = counter;
      this.opType = opType;
    }

    @Override
    public void run() {
      try {
        if (path != null) {
          Path f = new Path(path);
          fs = DaosFSFactory.getFS();
          switch (opType) {
            case 1:
              FSDataOutputStream fos = fs.create(f);
              if (fos != null) {
                try {
                  fos.close();
                } catch (Exception e) {
                  e.printStackTrace();
                  return;
                }
                counter.incrementAndGet();
              }
              break;
            case 2:
              FsPermission permission = new FsPermission("0755");
              if (fs.mkdirs(f, permission)) {
                counter.incrementAndGet();
              }
              break;
            case 3:
              DaosFileSystem daosFileSystem = new DaosFileSystem();
              Configuration conf = new Configuration(false);
              URI uri = URI.create(path);
              daosFileSystem.initialize(uri, conf);
              daosFileSystem.close();
              counter.incrementAndGet();
          }
        } else {
          fs = DaosFSFactory.getFS();
          try {
            Thread.sleep(1000);
          } catch (InterruptedException e) {
            e.printStackTrace();
          }
        }
      } catch (IOException e) {
        e.printStackTrace();
      }
    }
  }

  @Test
  public void testMultipleCreateFile() throws InterruptedException {
    AtomicInteger counter = new AtomicInteger(0);
    FileOperation fcs[] = {
        new FileOperation(
"/attempt_20191113114701_0024_m_000159_4855/part-00159-a32d-41f9-bbb7-b9867e4e99ff-c0000.csv", counter, 1),
        new FileOperation(
"/attempt_20191113114701_0024_m_000159_4855/part-00159-a32d-41f9-bbb7-b9867e4e99ff-c0001.csv", counter, 1),
        new FileOperation(
"/attempt_20191113114701_0024_m_000159_4855/part-00159-a32d-41f9-bbb7-b9867e4e99ff-c0002.csv", counter, 1),
        new FileOperation(
"/attempt_20191113114701_0024_m_000159_4855/part-00159-a32d-41f9-bbb7-b9867e4e99ff-c0003.csv", counter, 1),
        new FileOperation(null, counter, 1)
    };
    ExecutorService executor = Executors.newFixedThreadPool(5);
    for (FileOperation fc : fcs) {
      executor.submit(fc);
    }
    executor.shutdown();
    executor.awaitTermination(30, TimeUnit.SECONDS);
    Assert.assertEquals(4, counter.get());
  }

  @Test
  public void testMultipleMakeDirs() throws InterruptedException {
    AtomicInteger counter = new AtomicInteger(0);
    long suffix = System.currentTimeMillis();
    FileOperation fcs[] = {
        new FileOperation(
            "/attempt_20191113114701_0024" + suffix, counter, 2),
        new FileOperation(
            "/attempt_20191113114701_0024" + suffix, counter, 2),
        new FileOperation(
            "/attempt_20191113114701_0024" + suffix, counter, 2),
        new FileOperation(
            "/attempt_20191113114701_0024" + suffix, counter, 2),
        new FileOperation(null, counter, 2)
    };
    ExecutorService executor = Executors.newFixedThreadPool(5);
    for (FileOperation fc : fcs) {
      executor.submit(fc);
    }
    executor.shutdown();
    executor.awaitTermination(30, TimeUnit.SECONDS);
    Assert.assertEquals(4, counter.get());
  }

  @Test
  public void testMultipleDaosMount() throws Exception {
    AtomicInteger counter = new AtomicInteger(0);
    File file = Files.createTempDirectory("uns").toFile();
    String path = file.getAbsolutePath();
    String daosAttr = String.format(io.daos.Constants.DUNS_XATTR_FMT, Layout.POSIX.name(),
        DaosFSFactory.getPooluuid(), DaosFSFactory.getContuuid());
    DaosUns.setAppInfo(path, io.daos.Constants.DUNS_XATTR_NAME, daosAttr);

    String uriPath = "daos://" + io.daos.Constants.UNS_ID_PREFIX + System.currentTimeMillis() + path;
    FileOperation fcs[] = {
        new FileOperation(
            uriPath, counter, 3),
        new FileOperation(
            uriPath, counter, 3),
        new FileOperation(
            uriPath, counter, 3),
        new FileOperation(
            uriPath, counter, 3),
        new FileOperation(null, counter, 3)
    };
    ExecutorService executor = Executors.newFixedThreadPool(5);
    for (FileOperation fc : fcs) {
      executor.submit(fc);
    }
    executor.shutdown();
    executor.awaitTermination(30, TimeUnit.SECONDS);
    Assert.assertEquals(4, counter.get());
  }
}
