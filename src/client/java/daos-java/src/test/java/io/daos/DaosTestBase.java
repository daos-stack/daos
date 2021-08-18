package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "bce96e2f-5a52-4aeb-b0b4-8994eee79a5f";
  public static final String DEFAULT_CONT_ID = "e7814443-c8a1-4014-b94b-571ac7f5cff3";

  public static final String DEFAULT_OBJECT_CONT_ID = "734a0588-da98-4be2-9555-93703a6b69a9";

  public static String getPoolId() {
    return System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
  }

  public static String getContId() {
    return System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  public static String getObjectContId() {
    return System.getProperty("object_cont_id", DaosTestBase.DEFAULT_OBJECT_CONT_ID);
  }
}
