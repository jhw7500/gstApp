/*
 *
 * Cantops parser.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */
/*
 * Pointimage source/application/utils/json_c.h support
 */

#ifndef __JSONC_HEADER__
#define __JSONC_HEADER__

#ifdef __cplusplus
extern "C"{
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#define JSON_MAX_INDEX 100
#define JSON_LAST_ARG_MAGIC_NUMBER -1027
#define JSON_STRBUFSIZE 256

//-------------------------------------------------------------------------
typedef enum json_type_enum 
{ 
	JSON_UNDEFINED	= 0x0,
	JSON_NUMBER		= 0x1,
	JSON_STRING		= 0x2,
	JSON_BOOLEAN	= 0x4,
	JSON_ARRAY		= 0x8,
	JSON_OBJECT		= 0x10,
	JSON_NULL		= 0x20,
	JSON_INTEGER	= 0x40,
	JSON_DOUBLE		= 0x80 
} json_type;

//-------------------------------------------------------------------------
typedef enum json_keyorvalue_enum 
{ 
	JSON_KEY, 
	JSON_VALUE 
} json_keyorvalue;

//-------------------------------------------------------------------------
typedef struct json_value_s 
{
    json_type type;
    void* value;
} json_value;

//-------------------------------------------------------------------------
typedef struct json_object_s 
{
    int last_index;
    char* keys[JSON_MAX_INDEX];
    json_value values[JSON_MAX_INDEX];
} json_object;

//-------------------------------------------------------------------------
typedef struct json_array_s 
{
    int last_index;
    json_value values[JSON_MAX_INDEX];
} json_array;

//-------------------------------------------------------------------------
json_value json_string_to_value(const char** json_message);
json_value json_create(const char* json_message);
json_array * json_create_array(const char** json_message);
json_object * json_create_object(const char** json_message);

#define json_get_int(...) ((int)json_to_longlongint(json_get(__VA_ARGS__)))
#define json_get_bool(...) json_to_bool(json_get(__VA_ARGS__))
#define json_get_string(...) json_to_string(json_get(__VA_ARGS__))

long long int json_to_longlongint(json_value v);
bool json_to_bool(json_value v);
char * json_to_string(json_value v);
json_type json_get_type(json_value v);

#define json_get(...) (json_get_value(__VA_ARGS__, (void*)JSON_LAST_ARG_MAGIC_NUMBER))
json_value json_get_value(json_value v, ...);
json_value json_get_from_json_value(json_value v, const void* k);
json_value json_get_from_object(json_object* json, const char* key);
json_value json_get_from_array(json_array* json, const int index);

void json_free(json_value jsonv);
void json_free_array(json_array* jsona);
void json_free_object(json_object* jsono);

#ifdef __cplusplus
}
#endif
#endif
