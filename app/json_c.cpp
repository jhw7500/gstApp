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

#include "json_c.h"

#ifndef __JSONC_BODY__
#define __JSONC_BODY__

#ifdef __cplusplus
extern "C"{
#endif

static const json_value undefined_json = {JSON_UNDEFINED, NULL};

//-------------------------------------------------------------------------
json_value json_get_value(json_value v, ...) 
{
	void * key = NULL;
	void * vakey = NULL;
	va_list ap;

	va_start(ap, v);

	key = va_arg(ap, void *);
	if((long)key == JSON_LAST_ARG_MAGIC_NUMBER)
	{ 
		return v;
	}

	if(!(v.type == JSON_ARRAY || v.type == JSON_OBJECT))
	{
		fprintf(stderr, "json_get error\n");
		return undefined_json;
	}

	json_value ret = json_get_from_json_value(v, key);
	if(ret.type == JSON_UNDEFINED)
	{
		fprintf(stderr, "\n");
		return ret;
	}

	while(1)
	{
		vakey = va_arg(ap, void *);

		if((long)vakey == JSON_LAST_ARG_MAGIC_NUMBER) 
			break; 

		ret = json_get_from_json_value(ret, vakey);

		if(ret.type == JSON_UNDEFINED)
		{
			fprintf(stderr, "\n");
			return ret;
		}
	}
    return ret;
}

//-------------------------------------------------------------------------
json_value json_get_from_json_value(json_value v, const void* key) 
{
    if (v.type == JSON_OBJECT) 
		return json_get_from_object((json_object *)(v.value), (char *)key);

    if (v.type == JSON_ARRAY) 
		return json_get_from_array((json_array *)(v.value), (long)key);

	fprintf(stderr, "json_get_from_json_value error\n");

    return undefined_json;
}

//-------------------------------------------------------------------------
json_value json_get_from_object(json_object* json, const char* key) 
{
	if((long)key >=0 && (long)key <= json->last_index)
		return json->values[(long)key];

	if((long)key <= JSON_MAX_INDEX && (long)key>= 0)
	{
		fprintf(stderr, "json_get_from_object error\n");
		return undefined_json;
	}
		
    if (json == NULL || key == NULL || *key == '\0') 
		return undefined_json;

    for (int i = 0; i <= json->last_index; i++) 
	{
        if (strcmp(json->keys[i], key) == 0) 
		{
            return json->values[i];
        }
    }

	fprintf(stderr, "json_get_from_object error\n");

    return undefined_json;
}

//-------------------------------------------------------------------------
json_value json_get_from_array(json_array* json, const int index) 
{
    if (json == NULL || index < 0 || json->last_index < index)
	{
		fprintf(stderr, "json_get_from_array error\n");
		return undefined_json;
	}
    return json->values[index];
}

//-------------------------------------------------------------------------
json_value json_string_to_value(const char** json_message) 
{
	char c;
	char temp[64] = "";
	json_value jsonv;
	jsonv.type = JSON_UNDEFINED;
	jsonv.value = NULL;

	while (c = *((*json_message)++)) 
	{
		switch (c) 
		{
			case '{':
				(*json_message)--;
				jsonv.type = JSON_OBJECT;
				jsonv.value = json_create_object(json_message);
				return jsonv;
			case '}':
				printf("parse error : unexpected token '}'\n");
				return jsonv;
			case '[':
				(*json_message)--;
				jsonv.type = JSON_ARRAY;
				jsonv.value = json_create_array(json_message);
				return jsonv;
			case ']':
				printf("parse error : unexpected token ']'\n");
				return jsonv;
			case '\"':
				{
					jsonv.type = JSON_STRING;
					char* str = (char*)malloc(sizeof(char) * JSON_STRBUFSIZE);
					int size = 0;
					if (str == NULL) 
					{
						printf("string malloc error;\n");
						return jsonv;
					}

					while (true) 
					{
						char ch = *((*json_message)++);
						switch(ch)
						{
							case '\\':
								{
									char escape = *((*json_message)++);
									switch(escape)
									{
										case '\"': str[size] = '\"'; break;
										case '\\': str[size] = '\\'; break;
										case '/': str[size] = '/'; break;
										case 'b': str[size] = '\b'; break;
										case 'f': str[size] = '\f'; break;
										case 'n': str[size] = '\n'; break;
										case 'r': str[size] = '\r'; break;
										case 't': str[size] = '\t'; break;
										case 'u':{
													 str[size++] = '\\';
													 str[size] = 'u';
													 break;
												 }
										default:
												  fprintf(stderr, "json_string_to_value error\n");
									}
									break;
								}
							case '\"':
								str[size] = '\0';
								goto JSON_STRBREAK;
							default:
								str[size] = ch;
						}

						size++;

						if((size+1) % JSON_STRBUFSIZE == 0)
						{
							str = (char *)realloc(str, sizeof(char) * JSON_STRBUFSIZE * (1 + (size+1)/JSON_STRBUFSIZE));
							if(str == NULL)
							{
								printf("string malloc error;\n");
								return jsonv;
							}
						}
					}
JSON_STRBREAK:
					jsonv.value = str;
					return jsonv;
				}
			default:
				{
					if (isalpha(c)) 
					{
						const char* startptr = (*json_message) - 1;

						while (isalpha(*((*json_message)++)));

						(*json_message)--;

						int size = (*json_message - startptr);
						memcpy(temp, startptr, sizeof(char) * size);
						temp[size] = '\0';

						if (strcasecmp(temp, "null") == 0) 
						{
							jsonv.type = JSON_NULL;
							return jsonv;
						}

						if (strcasecmp(temp, "false") == 0 || strcasecmp(temp, "true") == 0) 
						{
							jsonv.type = JSON_BOOLEAN;
							jsonv.value = malloc(sizeof(bool));

							if (strcasecmp(temp, "false") == 0) 
								*((bool *)jsonv.value) = false;
							else 
								*((bool *)jsonv.value) = true;
							return jsonv;
						}
						printf("BOOLEAN or NULL error\n");
						return jsonv;
					}
					if (isdigit(c) || c == '-' || c == '+' || c == '.') 
					{
						const char* startptr = (*json_message) - 1;

						while(1) 
						{
							char ch = *((*json_message)++);
							if ((isdigit(ch) || ch == '.' || ch=='e' || ch=='E' || ch=='+' || ch == '-') == false)
								break;
						}

						(*json_message)--;

						int size = (*json_message - startptr);

						memcpy(temp, startptr, sizeof(char) * size);
						temp[size] = '\0';

						if(strchr(temp, '.') || strchr(temp, 'e') || strchr(temp, 'E'))
						{
							jsonv.type = (json_type) (JSON_NUMBER|JSON_DOUBLE);
							jsonv.value = malloc(sizeof(double));
							if (jsonv.value == NULL) 
							{
								printf("malloc error!\n");
								return jsonv;
							}
							*((double*)jsonv.value) = atof(temp);
						} 
						else
						{
							jsonv.type = (json_type) (JSON_NUMBER|JSON_INTEGER);
							jsonv.value = malloc(sizeof(long long int));
							if (jsonv.value == NULL) 
							{
								printf("malloc error!\n");
								return jsonv;
							}
							*((long long int*)jsonv.value) = atoll(temp);
						}
						return jsonv;
					}
				}
		}
	}

	fprintf(stderr, "json_string_to_value error\n");
	return jsonv;
}

//-------------------------------------------------------------------------
json_value json_create(const char* json_message) 
{
    return json_string_to_value(&json_message);
}

//-------------------------------------------------------------------------
json_array* json_create_array(const char** json_message) 
{
	json_array* jsona = (json_array *)malloc(sizeof(json_array));
	memset(jsona, 0x00, sizeof(json_array));
	jsona->last_index = 0;
	char c;
	int stack = 0;
	while (c = *((*json_message)++)) 
	{
		switch (c) 
		{
			case '[':
				if (stack == 0) 
					stack++;
				else 
				{
					(*json_message)--;
					jsona->values[jsona->last_index] = json_string_to_value(json_message);
					jsona->last_index++;
				}
				break;
			case ']':
				jsona->last_index--;
				return jsona;
			default:
				if (isalpha(c) || isdigit(c) || c == '[' || c == '{' || c == '\"' || c == '-' || c=='+' || c=='.') 
				{
					(*json_message)--; 
					jsona->values[jsona->last_index] = json_string_to_value(json_message);
					jsona->last_index++;
				}
				break;
		}
	}

	fprintf(stderr, "json_create_array error\n");
	return jsona;
}

//-------------------------------------------------------------------------
json_object* json_create_object(const char** json_message) 
{
    json_object* jsono = (json_object*)malloc(sizeof(json_object));
    memset(jsono, 0x00, sizeof(json_object));
    jsono->last_index = 0;
    int stack = 0;
    int keyorvalue = JSON_KEY;
    char c;

    while (c = *((*json_message)++)) 
	{
		switch (c) 
		{
			case '{':
				if (stack == 0) 
					stack++;
				else 
				{
					if (keyorvalue == JSON_KEY) 
					{
						printf("key cannot be an Object\n");
						return jsono;
					}
					(*json_message)--;
					jsono->values[jsono->last_index] = json_string_to_value(json_message);
					jsono->last_index++;
					keyorvalue = JSON_KEY;
				}
				break;
			case '}':
				jsono->last_index--;
				return jsono;
			default:
				if (isalpha(c) || isdigit(c) || c == '[' || c == '{' || c == '\"' || c == '-' || c=='+' || c=='.') 
				{
					(*json_message)--;
					if (keyorvalue == JSON_KEY) 
					{
						json_value v = json_string_to_value(json_message);
						if (v.type != JSON_STRING) 
						{
							printf("Key MUST be a string");
							return jsono;
						}
						jsono->keys[jsono->last_index] = (char *)(v.value);
						keyorvalue = JSON_VALUE;
					}
					else 
					{
						jsono->values[jsono->last_index] = json_string_to_value(json_message);
						jsono->last_index++;
						keyorvalue = JSON_KEY;
					}
				}
		}
	}

	fprintf(stderr, "json_create_object error\n");
	return jsono;
}

//-------------------------------------------------------------------------
long long int json_to_longlongint(json_value v)
{
	if(!(v.type & JSON_NUMBER))
	{
		fprintf(stderr, "json_to_longlongint error\n");
		return 0;
	}

	if( v.type & JSON_INTEGER ) 
		return *((long long int *)(v.value));

	if( v.type & JSON_DOUBLE ) 
		return (long long int)*((double *)(v.value));

	fprintf(stderr, "json_to_longlongint error\n");
	return 0;
}

//-------------------------------------------------------------------------
bool json_to_bool(json_value v)
{
	if(!(v.type & JSON_BOOLEAN))
	{
		fprintf(stderr, "json_to_bool error\n");
		return false;
	}

	return *((bool *)(v.value));
}

//-------------------------------------------------------------------------
char * json_to_string(json_value v)
{
	if(!(v.type & JSON_STRING))
	{
		fprintf(stderr, "json_to_string error\n");
		return NULL;
	}

	return (char *)(v.value);
}

//-------------------------------------------------------------------------
json_type json_get_type(json_value v)
{
	return v.type;
}

//-------------------------------------------------------------------------
void json_free(json_value jsonv) 
{
    int t = jsonv.type;

	if(t == JSON_NUMBER || t == JSON_STRING || t == JSON_BOOLEAN) 
	{
		free(jsonv.value);
    }
    else if(t == JSON_ARRAY) 
	{
        json_free_array((json_array *)(jsonv.value));
    }
    else if(t == JSON_OBJECT) 
	{
        json_free_object((json_object *)(jsonv.value));
    }
}

//-------------------------------------------------------------------------
void json_free_array(json_array* jsona) 
{
    if (jsona == NULL) 
		return;

    for (int i = 0; i <= jsona->last_index; i++)
        json_free(jsona->values[i]);
    free(jsona);
}

//-------------------------------------------------------------------------
void json_free_object(json_object* jsono) 
{
    if (jsono == NULL) 
		return;

    for (int i = 0; i <= jsono->last_index; i++) 
	{
        free(jsono->keys[i]);
        json_free(jsono->values[i]);
    }
    free(jsono);
}

#ifdef __cplusplus
}
#endif
#endif
