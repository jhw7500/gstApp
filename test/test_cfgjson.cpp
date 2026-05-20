/* cfg_get_int_array 단위테스트 — standalone(assert 기반, Check 프레임워크 비의존).
 * json-c + glib만 링크 → 보드(192.168.0.200) 런타임에 둘 다 존재하므로 그대로 실행 가능. */
#include <cstdio>
#include <glib.h>
#include <json-c/json.h>

#include "../cfgjson.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                   \
  do {                                                               \
    g_checks++;                                                      \
    if (!(cond)) {                                                   \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
      g_failures++;                                                  \
    }                                                                \
  } while (0)

static json_object *J(const char *s) { return json_tokener_parse(s); }

int main(void) {
  /* 1. 정확한 길이(2) → OK + populate */
  {
    json_object *o = J("{\"bps\":[8000,1024]}");
    gint out[2] = {0, 0};
    CfgArrStatus st = cfg_get_int_array(o, "bps", out, 2);
    CHECK(st == CFG_ARR_OK);
    CHECK(out[0] == 8000);
    CHECK(out[1] == 1024);
    json_object_put(o);
  }
  /* 2. 길이 부족(1!=2) → BAD_LEN + 기본값 유지 (실제 4096 고정 버그 회귀) */
  {
    json_object *o = J("{\"bps\":[8000]}");
    gint out[2] = {4096, 1024};
    CfgArrStatus st = cfg_get_int_array(o, "bps", out, 2);
    CHECK(st == CFG_ARR_BAD_LEN);
    CHECK(out[0] == 4096);
    CHECK(out[1] == 1024);
    json_object_put(o);
  }
  /* 3. 길이 초과(3!=2) → BAD_LEN (MAX_MODE=3 vs bps[2] 불일치 버그) */
  {
    json_object *o = J("{\"bps\":[1,2,3]}");
    gint out[2] = {4096, 1024};
    CfgArrStatus st = cfg_get_int_array(o, "bps", out, 2);
    CHECK(st == CFG_ARR_BAD_LEN);
    CHECK(out[0] == 4096);
    json_object_put(o);
  }
  /* 4. 배열 아님 → NOT_ARRAY + 유지 */
  {
    json_object *o = J("{\"bps\":4096}");
    gint out[2] = {4096, 1024};
    CfgArrStatus st = cfg_get_int_array(o, "bps", out, 2);
    CHECK(st == CFG_ARR_NOT_ARRAY);
    CHECK(out[0] == 4096);
    json_object_put(o);
  }
  /* 5. 키 없음 → MISSING + 유지 */
  {
    json_object *o = J("{}");
    gint out[2] = {4096, 1024};
    CfgArrStatus st = cfg_get_int_array(o, "bps", out, 2);
    CHECK(st == CFG_ARR_MISSING);
    CHECK(out[0] == 4096);
    json_object_put(o);
  }
  /* 6. 원소 타입 불량 → BAD_ELEM + 부분 변경 없음 */
  {
    json_object *o = J("{\"bps\":[8000,\"x\"]}");
    gint out[2] = {4096, 1024};
    CfgArrStatus st = cfg_get_int_array(o, "bps", out, 2);
    CHECK(st == CFG_ARR_BAD_ELEM);
    CHECK(out[0] == 4096);
    json_object_put(o);
  }

  printf("\ncfgjson test: %d checks, %d failures -> %s\n", g_checks, g_failures,
         g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
