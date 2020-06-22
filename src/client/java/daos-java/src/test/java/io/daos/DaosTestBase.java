package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "322f9095-2639-4a1b-94f9-2234b5b16bbf";
  public static final String DEFAULT_CONT_ID = "a2a84a5f-06d3-42eb-861c-97ddd83f1fae";

  public static final String DEFAULT_OBJECT_CONT_ID = "a2a84a5f-06d3-42eb-861c-97ddd83f1fae";

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
