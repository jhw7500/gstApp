#ifndef CFGJSON_H
#define CFGJSON_H

#include <glib.h>
#include <json-c/json.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CFG_ARR_OK = 0,    /* out[0..n) populated */
  CFG_ARR_MISSING,   /* key absent — out unchanged */
  CFG_ARR_NOT_ARRAY, /* present but not a JSON array — out unchanged */
  CFG_ARR_BAD_LEN,   /* array length != n — out unchanged */
  CFG_ARR_BAD_ELEM   /* an element is not int — out unchanged */
} CfgArrStatus;

/*
 * Read int array `name` from `obj` into out[0..n).
 * out is mutated ONLY when the return value is CFG_ARR_OK (all-or-nothing).
 * Pure: no logging, no global state — caller decides logging / error counting.
 */
CfgArrStatus cfg_get_int_array(json_object *obj, const char *name, gint *out,
                               gsize n);

typedef enum {
  CFG_BOOL_OK = 0,
  CFG_BOOL_MISSING,
  CFG_BOOL_BAD_TYPE,
  CFG_BOOL_BAD_VALUE
} CfgBoolStatus;

CfgBoolStatus cfg_get_bool(json_object *obj, const char *name, gboolean *out);

#ifdef __cplusplus
}
#endif

#endif /* CFGJSON_H */
