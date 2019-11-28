package com.intel.daos;

import com.intel.daos.DaosDirectory;
import com.intel.daos.DaosFS;
import com.intel.daos.DaosFile;
import com.intel.daos.DaosObjClass;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Random;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

import static org.junit.Assert.assertEquals;

/**
 *
 */
public class DaosFSTest {

  final static int KB = 1 << 10;
  final static int MB = 1 << 20;
  final static int GB = 1 << 30;

  private static DaosFS dfs;


  private static void printArray(byte[] buf, int offset, int number) {
    System.out.println("Printing array of length: " + buf.length);
    for (int i = offset; i < offset + number; i++) {
      System.out.printf("%d ", buf[i]);
    }
    System.out.println();
  }

  private static boolean compareArray(
          byte[] a,
          byte[] b,
          int indexA,
          int indexB,
          int length) {
    for (int i = 0; i < length; i++) {
      if (a[indexA + i] != b[indexB + i]) {
        System.out.printf("a[%d] != b[%d]!.\n", indexA + i, indexB + i);
        return false;
      }
    }
    return true;
  }

  private static void list(String path)
      throws IOException {
    String[] dirs = dfs.listDir(path);
    for (String dir : dirs) {
      String ent = path.endsWith("/") ? path + dir : path + "/" + dir;
      System.out.println(ent);
      if (dfs.isDir(ent)) {
        list(ent);
      }
    }
  }

  private static void listAll()
      throws IOException {
    System.out.println("Printing all contents in dfs:");
    list("/");
  }

  @BeforeClass
  public static void getFS()
      throws IOException, IOException {
    long start = System.nanoTime();
    dfs = new DaosFS.Builder(true, false, true).setScm(50*1024*1024).setNvme(0).build();
    long end = System.nanoTime();
    System.out.println((end - start) / 1000000000.0);
  }

  @Test
  public void dirTest()
        throws IOException {
    String path = "/foo" + System.currentTimeMillis();
    String newPath = "/bar" + System.currentTimeMillis();
    if (dfs.ifExist(path)) {
      dfs.remove(path);
    }
    DaosDirectory dir = dfs.createDir(path, 0777);
    dir.close();
    listAll();
    dfs.move(path, newPath);
    listAll();
    dfs.remove(newPath);
    listAll();
  }

  @Test
  public void fileTest()
        throws IOException {
    String path = "/demo" + System.currentTimeMillis();
    int size = 5 * MB;
    int loop = 2;
    ByteBuffer buffer = ByteBuffer.allocateDirect(size);
    Random rand = new Random();
    byte[] randArray = new byte[size];
    DaosFile demo = dfs.createFile(path, 0777,
            8 * KB, DaosObjClass.OC_SX);
    rand.nextBytes(randArray);
    buffer.put(randArray);
    // write file test
    for (int i = 0; i < loop; i++) {
      demo.write(i * size, buffer, size);
    }
    printArray(randArray, size < 100 ? 0 : size - 100, size < 100 ? size : 100);
    // size test
    assertEquals(demo.size(), size * loop);
    //read test
    buffer.clear();
    demo.read(0, buffer);
    Arrays.fill(randArray, (byte)0);
    buffer.get(randArray);
    printArray(randArray, size < 100 ? 0 : size - 100, size < 100 ? size : 100);
    demo.close();
    dfs.remove(path);
  }

  @Test
  public void multiThreadTest()
      throws IOException, InterruptedException {
    ExecutorService exec = Executors.newFixedThreadPool(2);
    int size = MB;
    String path1 = "/test1" + System.currentTimeMillis();
    String path2 = "/test2" + System.currentTimeMillis();
    exec.submit(() -> {
      try {
        DaosFile file = dfs.createFile(path1, 0777,
                8 * KB, DaosObjClass.OC_SX);
        ByteBuffer buffer = ByteBuffer.allocateDirect(size);
        byte[] randArray = new byte[size];
        new Random().nextBytes(randArray);
        buffer.put(randArray);
        file.write(0, buffer, size);
        file.close();
      } catch (Exception e) {
        e.printStackTrace();
      }
    });
    exec.submit(() -> {
      try {
        DaosFile file = dfs.createFile(path2, 0777,
                8 * KB, DaosObjClass.OC_SX);
        ByteBuffer buffer = ByteBuffer.allocateDirect(size);
        byte[] randArray = new byte[size];
        new Random().nextBytes(randArray);
        buffer.put(randArray);
        file.write(0, buffer, size);
        file.close();
      } catch (Exception e) {
        e.printStackTrace();
      }
    });
    exec.submit(() -> {
      try {
        while (!dfs.ifExist(path1)){
          Thread.sleep(200);
        }
        DaosFile file = dfs.getFile(path1, false);
        ByteBuffer buffer = ByteBuffer.allocateDirect(size);
        long readSize = file.read(0, buffer);
        file.close();
        assertEquals(readSize, size);
      } catch (Exception e) {
        e.printStackTrace();
      }
    });
    exec.submit(() -> {
      try {
        while (!dfs.ifExist(path2)){
          Thread.sleep(200);
        }
        DaosFile file = dfs.getFile(path2, false);
        ByteBuffer buffer = ByteBuffer.allocateDirect(size);
        long readSize = file.read(0, buffer);
        file.close();
        assertEquals(readSize, size);
      } catch (Exception e) {
        e.printStackTrace();
      }
    });
    exec.shutdown();
    exec.awaitTermination(30, TimeUnit.SECONDS);
    dfs.remove(path1);
    dfs.remove(path2);
  }

  @AfterClass
  public static void tearDown(){
    if(!dfs.closeContainer()){
      System.err.println("close contianer fail");
    }
    if(!dfs.disconnectPool()){
      System.err.println("disconnect pool fail");
    }
    if(!dfs.destroyPool()){
      System.err.println("destroy pool failed !! ");
    }
  }
}
