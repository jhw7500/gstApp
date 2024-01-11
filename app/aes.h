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

#pragma once
#ifndef _AES_H_
#define _AES_H_

typedef unsigned int            UINT;
typedef unsigned char           BYTE;
typedef unsigned char*          LPBYTE;
typedef const unsigned char*    LPCBYTE;
#define VOID                    void
#define CONST                   const

#define LOCAL(type) static type //WINAPI

VOID AES_ECB_Encrypt(LPCBYTE Input, LPCBYTE Key, LPBYTE Output, int Length);
VOID AES_ECB_Decrypt(LPCBYTE Input, LPCBYTE Key, LPBYTE Output, int Length);
int encrypt_get_passwd(char *filename, char *passwd);
int encrypt_change_passwd(char *filename, char *cur_passwd, const char *change_passwd);

#endif //_AES_H_
