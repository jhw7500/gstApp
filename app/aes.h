/*
 *
 * Cantops aes.cpp support
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

#include "util.h"

#define DEFAULT_PASSWD_PATH "/root/shared_v/.passwd"
typedef unsigned int            UINT;
typedef unsigned char           BYTE;
typedef unsigned char*          LPBYTE;
typedef const unsigned char*    LPCBYTE;
#define VOID                    void
#define CONST                   const

#define LOCAL(type) static type //WINAPI

class AESClass
{
public :
	static AESClass* getInstance();
    AESClass();
    ~AESClass();
    int encrypt_get_passwd(const char *filename, char *passwd);
    int encrypt_change_passwd(const char *filename, char *cur_passwd, const char *change_passwd);

private :
	VOID AES_ECB_Encrypt(LPCBYTE Input, LPCBYTE Key, LPBYTE Output, int Length);
    VOID AES_ECB_Decrypt(LPCBYTE Input, LPCBYTE Key, LPBYTE Output, int Length);
    VOID GetSBox(LPBYTE TA);
    VOID KeyExpansion(LPBYTE ExpKey, LPCBYTE Key) ;
    VOID AddRoundKey(BYTE State[4][4], LPBYTE ExpKey, BYTE Round);
    VOID SubBytes(BYTE State[4][4]);
    VOID ShiftRows(BYTE State[4][4]);
    VOID InvShiftRows(BYTE State[4][4]);
    int XTime(int X);
    VOID MixColumns(BYTE State[4][4]);
    int Multiply(int X, int Y);
    VOID InvMixColumns(BYTE State[4][4]);
    VOID InvSubBytes(BYTE State[4][4]);
    VOID InvCipher(BYTE State[4][4], LPBYTE ExpKey) ;
    VOID Cipher(BYTE State[4][4], LPBYTE ExpKey);

public :
	
private :

};

#endif //_AES_H_
