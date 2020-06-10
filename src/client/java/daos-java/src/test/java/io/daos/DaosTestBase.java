package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "6112d3ac-f99b-4e46-a2ab-549d9d56c069";
  public static final String DEFAULT_CONT_ID = "10e8b68a-c80a-4840-84fe-3b707ebb5475";

  public static final String DEFAULT_OBJECT_CONT_ID = "310b4737-7c4f-42a7-87ee-af16a477499b";

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
