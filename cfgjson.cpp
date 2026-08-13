#include "cfgjson.h"

/*
 * Reads int array `name` into out[0..n). Mutates out ONLY on CFG_ARR_OK
 * (all elements validated first → all-or-nothing).
 *
 * Strict length match (arr->length == n) is intentional: a mismatch means the
 * edgeconf array shape disagrees with the code's expectation, so silently
 * accepting a partial/oversized array would hide a real config error.
 */
CfgArrStatus cfg_get_int_array(json_object *obj, const char *name, gint *out,
                               gsize n) {
  if (!obj || !name || !out || n == 0)
    return CFG_ARR_MISSING;

  json_object *vobj = json_object_object_get(obj, name);
  if (!vobj)
    return CFG_ARR_MISSING;

  if (json_object_get_type(vobj) != json_type_array)
    return CFG_ARR_NOT_ARRAY;

  array_list *arr = json_object_get_array(vobj);
  if (!arr || (gsize)arr->length != n)
    return CFG_ARR_BAD_LEN;

  for (gsize i = 0; i < n; i++) {
    json_object *el = (json_object *)array_list_get_idx(arr, i);
    if (!el || json_object_get_type(el) != json_type_int)
      return CFG_ARR_BAD_ELEM;
  }

  for (gsize i = 0; i < n; i++) {
    json_object *el = (json_object *)array_list_get_idx(arr, i);
    out[i] = json_object_get_int(el);
  }
  return CFG_ARR_OK;
}

CfgBoolStatus cfg_get_bool(json_object *obj, const char *name, gboolean *out) {
  if (!obj || !name || !out)
    return CFG_BOOL_MISSING;
  json_object *value = json_object_object_get(obj, name);
  if (!value)
    return CFG_BOOL_MISSING;
  enum json_type type = json_object_get_type(value);
  if (type == json_type_boolean) {
    *out = json_object_get_boolean(value) ? TRUE : FALSE;
    return CFG_BOOL_OK;
  }
  if (type != json_type_int)
    return CFG_BOOL_BAD_TYPE;
  gint number = json_object_get_int(value);
  if (number != 0 && number != 1)
    return CFG_BOOL_BAD_VALUE;
  *out = number == 1 ? TRUE : FALSE;
  return CFG_BOOL_OK;
}
