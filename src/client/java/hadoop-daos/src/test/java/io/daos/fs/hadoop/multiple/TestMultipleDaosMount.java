package io.daos.fs.hadoop.multiple;

//import io.daos.fs.hadoop.CreateDaosFS;
import org.apache.hadoop.fs.FileSystem;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

/**
 *
 */
public class TestMultipleDaosMount {
//  private static FileSystem daos;
//  private static CreateDaosFS daosFS;
//
//  @BeforeClass
//  public static void testDaosInit() throws IOException {
//    daosFS=new CreateDaosFS();
//    daosFS.getPool();
//    daosFS.getContainer();
//
//  }
//
//  @AfterClass
//  public static void close(){
//    System.out.println("@AfterClass");
//    daosFS.close();
//  }
//
//  @Test
//  public void testMultipleMount() throws InterruptedException {
//    ExecutorService executor = Executors.newFixedThreadPool(5);
//    executor.submit(new Runnable() {
//      @Override
//      public void run() {
//        try {
//          daos=daosFS.getFs();
//          Thread.sleep(15000);
//        } catch (InterruptedException e) {
//          e.printStackTrace();
//        } finally {
//          try {
//            daos.close();
//          } catch (IOException e) {
//            e.printStackTrace();
//          }
//        }
//
//      }
//    });
//    executor.submit(new Runnable() {
//      @Override
//      public void run() {
//        try {
//          daos=daosFS.getFs();
//          Thread.sleep(10000);
//        } catch (InterruptedException e) {
//          e.printStackTrace();
//        } finally {
//          try {
//            daos.close();
//          } catch (IOException e) {
//            e.printStackTrace();
//          }
//        }
//
//      }
//    });
//    executor.submit(new Runnable() {
//      @Override
//      public void run() {
//        try {
//          daos=daosFS.getFs();
//          Thread.sleep(5000);
//        } catch (InterruptedException e) {
//          e.printStackTrace();
//        } finally {
//          try {
//            daos.close();
//          } catch (IOException e) {
//            e.printStackTrace();
//          }
//        }
//
//      }
//    });
//    executor.submit(new Runnable() {
//      @Override
//      public void run() {
//        try {
//          daos=daosFS.getFs();
//          Thread.sleep(3000);
//        } catch (InterruptedException e) {
//          e.printStackTrace();
//        } finally {
//          try {
//            daos.close();
//          } catch (IOException e) {
//            e.printStackTrace();
//          }
//        }
//
//      }
//    });
//    executor.submit(new Runnable() {
//      @Override
//      public void run() {
//        try {
//          daos=daosFS.getFs();
//          Thread.sleep(1000);
//        } catch (InterruptedException e) {
//          e.printStackTrace();
//        } finally {
//          try {
//            daos.close();
//          } catch (IOException e) {
//            e.printStackTrace();
//          }
//        }
//
//      }
//    });
//    executor.shutdown();
//    executor.awaitTermination(30, TimeUnit.SECONDS);
//  }
}
