// Parses SQLite3 file format (https://www.sqlite.org/fileformat.html)
// Supports: SELECT cols, INNER JOIN, WHERE (=, >, IS NOT NULL, AND), ORDER BY (with length()), table aliases, string/int literals
// Replacing 'sqlite-amalgamation-3510100' from < v1.1.1
#include "SQLoot.h"

// ===============================================================================================================================================================
// INTERNAL HELPER DECLARATIONS
// ===============================================================================================================================================================

// Basic helpers 
static BOOL ReadFileFromDiskA(IN LPCSTR pszFilePath, OUT PBYTE* ppFileBuffer, OUT PDWORD pdwFileSize);
static WORD ReadBigEndian16(IN PBYTE pb);
static DWORD ReadBigEndian32(IN PBYTE pb);
static INT64 ReadBigEndianInt(IN PBYTE pb, IN INT nBytes);
static DWORD ReadVarint(IN PBYTE pb, OUT PINT64 pi64Value);
static PBYTE GetPage(IN PSQLOOT_DB pDb, IN DWORD dwPageNumber);
static PBYTE ReadPayloadWithOverflow(IN PSQLOOT_DB pDb, IN PBYTE pbLocalPayload, IN DWORD dwLocalSize, IN DWORD dwTotalSize, IN DWORD dwFirstOverflowPage, OUT PDWORD pdwActualSize);
static BOOL ParsePageInfo(IN PBYTE pbPage, IN DWORD dwPageSize, IN BOOL bIsFirstPage, OUT PSQLOOT_PAGE_INFO pInfo);
static LPSTR DuplicateStringA(IN LPCSTR pszSrc);
static LPSTR TrimWhitespace(IN LPSTR pszStr);
static LPCSTR SkipWhitespace(IN LPCSTR psz);

// Cell and record parsing
static BOOL ParseCell(IN PSQLOOT_DB pDb, IN PBYTE pbPage, IN DWORD dwCellOffset, IN BYTE bPageType, IN BOOL bIsFirstPage, OUT PSQLOOT_CELL pCell);
static BOOL ParseRecord(IN PBYTE pbPayload, IN DWORD dwPayloadSize, OUT PSQLOOT_ROW pRow);
static VOID FreeRow(IN PSQLOOT_ROW pRow);
static VOID FreeExpression(IN PSQLOOT_EXPR pExpr);

// Schema parsing
static BOOL ParseSchema(IN PSQLOOT_DB pDb);

// Table iteration
static BOOL InitTableIterator(IN PSQLOOT_DB pDb, IN OUT PSQLOOT_TABLE_REF pTableRef);
static VOID ResetTableIterator(IN OUT PSQLOOT_TABLE_REF pTableRef);
static VOID FreeTableRef(IN OUT PSQLOOT_TABLE_REF pTableRef);
static BOOL NextTableRow(IN PSQLOOT_DB pDb, IN OUT PSQLOOT_TABLE_REF pTableRef);
static BOOL SeekTableRowById(IN PSQLOOT_DB pDb, IN OUT PSQLOOT_TABLE_REF pTableRef, IN INT64 i64TargetRowId);

// SQL Parsing
static BOOL ParseToken(IN OUT LPCSTR* ppszSql, OUT LPSTR pszToken, IN DWORD dwMaxLen);
static BOOL ParseColumnRef(IN LPCSTR pszToken, OUT PSQLOOT_COLUMN_REF pColRef);
static PSQLOOT_EXPR ParsePrimaryExpression(IN LPCSTR* ppszSql);
static PSQLOOT_EXPR ParseCompareExpression(IN LPCSTR* ppszSql);
static PSQLOOT_EXPR ParseAndExpression(IN LPCSTR* ppszSql);
static PSQLOOT_EXPR ParseExpression(IN LPCSTR* ppszSql);
static BOOL FindTableByNameOrAlias(IN PSQLOOT_STMT pStmt, IN LPCSTR pszName, OUT PINT pnIndex);
static BOOL IsColumnRowId(IN PSQLOOT_DB pDb, IN DWORD dwTableInfoIndex, IN LPCSTR pszColumn);
static VOID AnalyzeJoinForSeek(IN PSQLOOT_STMT pStmt, IN DWORD dwJoinIndex);
static BOOL ParseSQL(IN PSQLOOT_DB pDb, IN LPCSTR pszSql, OUT PSQLOOT_STMT pStmt);
static VOID ParseCreateTableColumns(IN OUT PSQLOOT_TABLE_INFO pTableInfo);

// Expression evaluation
static PSQLOOT_VALUE GetColumnValue(IN PSQLOOT_STMT pStmt, IN PSQLOOT_COLUMN_REF pColRef);
static BOOL EvaluateExprToValue(IN PSQLOOT_STMT pStmt, IN PSQLOOT_EXPR pExpr, OUT PSQLOOT_VALUE pResult);
static INT CompareValues(IN PSQLOOT_VALUE pLeft, IN PSQLOOT_VALUE pRight);
static BOOL EvaluateCondition(IN PSQLOOT_STMT pStmt, IN PSQLOOT_EXPR pExpr);
static BOOL CopyValue(IN PSQLOOT_VALUE pSrc, OUT PSQLOOT_VALUE pDst);
static BOOL DuplicateRow(IN PSQLOOT_ROW pSrc, OUT PSQLOOT_ROW pDst);
static INT CompareRowsForSort(const void* pA, const void* pB);
static BOOL AddRowToBuffer(IN PSQLOOT_STMT pStmt, IN PSQLOOT_ROW pRow);
static VOID SortResultBuffer(IN PSQLOOT_STMT pStmt);
static BOOL BuildResultRow(IN PSQLOOT_STMT pStmt);

// Result building
static BOOL BuildResultRow(IN PSQLOOT_STMT pStmt);

// ===============================================================================================================================================================
// GLOBAL VARIABLES
// ===============================================================================================================================================================

// Global error message buffer
#ifdef SQLOOT_DEBUG
static CHAR g_szErrorMsg[1024] = { 0 };
#endif 

// Global pointer for qsort comparison
static PSQLOOT_STMT g_pSortStmt = NULL;

// ===============================================================================================================================================================
// MACROS
// ===============================================================================================================================================================

#ifdef SQLOOT_DEBUG
#define SQLOOT_SET_ERROR(fmt, ...)     StringCchPrintfA(g_szErrorMsg, ARRAYSIZE(g_szErrorMsg), fmt, __VA_ARGS__)
#else
#define SQLOOT_SET_ERROR(fmt, ...)     ((void)0)
#endif

// ===============================================================================================================================================================
// INTERNAL HELPER IMPLEMENTATIONS
// ===============================================================================================================================================================

#pragma region HELPER_FUNCTIONS

static BOOL ReadFileFromDiskA(IN LPCSTR pszFilePath, OUT PBYTE* ppFileBuffer, OUT PDWORD pdwFileSize)
{
    HANDLE  hFile       = INVALID_HANDLE_VALUE;
    DWORD   dwFileSize  = 0x00,
            dwBytesRead = 0x00;
    PBYTE   pbBuffer    = NULL;

    if (!pszFilePath || !ppFileBuffer || !pdwFileSize)
        return FALSE;

    *ppFileBuffer   = NULL;
    *pdwFileSize    = 0x00;

    if ((hFile = CreateFileA(pszFilePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
    {
        SQLOOT_SET_ERROR("CreateFileA Failed With Error: %lu [Line:%d]", GetLastError(), __LINE__);
        return FALSE;
    }

    if ((dwFileSize = GetFileSize(hFile, NULL)) == INVALID_FILE_SIZE || dwFileSize == 0)
    {
        SQLOOT_SET_ERROR("GetFileSize Failed With Error: %lu [Line:%d]", GetLastError(), __LINE__);
        goto _END_OF_FUNC;
    }

    if (!(pbBuffer = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwFileSize + 1)))
    {
        SQLOOT_SET_ERROR("HeapAlloc Failed With Error: %lu [Line:%d]", GetLastError(), __LINE__);
        goto _END_OF_FUNC;
    }

    if (!ReadFile(hFile, pbBuffer, dwFileSize, &dwBytesRead, NULL) || dwBytesRead != dwFileSize)
    {
        SQLOOT_SET_ERROR("ReadFile Failed With Error: %lu [Line:%d]", GetLastError(), __LINE__);
        goto _END_OF_FUNC;
    }

    *ppFileBuffer  = pbBuffer;
    *pdwFileSize   = dwBytesRead;

_END_OF_FUNC:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    if (pbBuffer && !*ppFileBuffer)
        HeapFree(GetProcessHeap(), 0x00, pbBuffer);
    return (*ppFileBuffer && *pdwFileSize) ? TRUE : FALSE;
}

static WORD ReadBigEndian16(IN PBYTE pb)
{
    if (!pb) return 0;
    return (WORD)((pb[0] << 8) | (WORD)pb[1]);
}

static DWORD ReadBigEndian32(IN PBYTE pb)
{
    if (!pb) return 0;
    return ((DWORD)pb[0] << 24) | ((DWORD)pb[1] << 16) | ((DWORD)pb[2] << 8) | (DWORD)pb[3];
}

static INT64 ReadBigEndianInt(IN PBYTE pb, IN INT nBytes)
{
    INT64   i64Value    = 0;
    INT     i           = 0;
    BOOL    bNegative   = FALSE;

    if (!pb || nBytes <= 0 || nBytes > 8)
        return 0;

    if (pb[0] & 0x80)
        bNegative = TRUE;

    for (i = 0; i < nBytes; i++)
        i64Value = (i64Value << 8) | pb[i];

    if (bNegative && nBytes < 8)
    {
        INT64 i64Mask = ~((1LL << (nBytes * 8)) - 1);
        i64Value |= i64Mask;
    }

    return i64Value;
}

static DWORD ReadVarint(IN PBYTE pb, OUT PINT64 pi64Value)
{
    DWORD   dwBytesRead = 0;
    INT64   i64Value    = 0;
    BYTE    bByte       = 0;
    INT     i           = 0;

    if (!pb || !pi64Value)
        return 0;

    *pi64Value = 0;

    for (i = 0; i < 9; i++)
    {
        bByte = pb[i];

        if (i == 8)
        {
            i64Value = (i64Value << 8) | bByte;
            dwBytesRead = 9;
            break;
        }
        else
        {
            i64Value = (i64Value << 7) | (bByte & 0x7F);
            dwBytesRead++;

            if (!(bByte & 0x80))
                break;
        }
    }

    *pi64Value = i64Value;
    return dwBytesRead;
}

static PBYTE GetPage(IN PSQLOOT_DB pDb, IN DWORD dwPageNumber)
{
    DWORD dwOffset = 0;

    if (!pDb || !pDb->pbFileData || dwPageNumber == 0 || dwPageNumber > pDb->dwPageCount)
        return NULL;

    dwOffset = (dwPageNumber - 1) * pDb->dwPageSize;

    if (dwOffset >= pDb->dwFileSize)
        return NULL;

    return pDb->pbFileData + dwOffset;
}

static PBYTE ReadPayloadWithOverflow(IN PSQLOOT_DB pDb, IN PBYTE pbLocalPayload, IN DWORD dwLocalSize, IN DWORD dwTotalSize, IN DWORD dwFirstOverflowPage, OUT PDWORD pdwActualSize)
{
    PBYTE   pbFullPayload   = NULL;
    DWORD   dwCopied        = 0;
    DWORD   dwCurrentPage   = 0;
    PBYTE   pbPage          = NULL;
    DWORD   dwUsableSize    = 0;
    DWORD   dwOverflowSize  = 0;

    if (!pDb || !pbLocalPayload || !pdwActualSize)
        return NULL;

    *pdwActualSize = 0;

    pbFullPayload = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwTotalSize + 1);
    if (!pbFullPayload)
        return NULL;

    CopyMemory(pbFullPayload, pbLocalPayload, dwLocalSize);
    dwCopied = dwLocalSize;

    dwCurrentPage = dwFirstOverflowPage;
    dwUsableSize = pDb->dwPageSize - 4;

    while (dwCurrentPage != 0 && dwCopied < dwTotalSize)
    {
        pbPage = GetPage(pDb, dwCurrentPage);
        if (!pbPage)
            break;

        dwCurrentPage = ReadBigEndian32(pbPage);

        dwOverflowSize = dwTotalSize - dwCopied;
        if (dwOverflowSize > dwUsableSize)
            dwOverflowSize = dwUsableSize;

        CopyMemory(pbFullPayload + dwCopied, pbPage + 4, dwOverflowSize);
        dwCopied += dwOverflowSize;
    }

    *pdwActualSize = dwCopied;
    return pbFullPayload;
}

static BOOL ParsePageInfo(IN PBYTE pbPage, IN DWORD dwPageSize, IN BOOL bIsFirstPage, OUT PSQLOOT_PAGE_INFO pInfo)
{
    PBYTE   pbHeader    = NULL;
    DWORD   dwOffset    = 0;

    if (!pbPage || !pInfo)
        return FALSE;

    ZeroMemory(pInfo, sizeof(SQLOOT_PAGE_INFO));

    dwOffset = bIsFirstPage ? SQLOOT_HEADER_SIZE : 0;
    pbHeader = pbPage + dwOffset;

    pInfo->bPageType            = pbHeader[0];
    pInfo->wFirstFreeblock      = ReadBigEndian16(pbHeader + 1);
    pInfo->wCellCount           = ReadBigEndian16(pbHeader + 3);
    pInfo->wCellContentStart    = ReadBigEndian16(pbHeader + 5);
    pInfo->bFragmentedFreeBytes = pbHeader[7];

    if (pInfo->wCellContentStart == 0)
        pInfo->wCellContentStart = (WORD)65536;

    if (pInfo->bPageType == SQLOOT_PAGE_INTERIOR_TABLE || pInfo->bPageType == SQLOOT_PAGE_INTERIOR_INDEX)
        pInfo->dwRightMostPointer = ReadBigEndian32(pbHeader + 8);

    return TRUE;
}

static LPSTR DuplicateStringA(IN LPCSTR pszSrc)
{
    SIZE_T  cchLen  = 0;
    LPSTR   pszDst  = NULL;

    if (!pszSrc)
        return NULL;

    cchLen = lstrlenA(pszSrc);

    if (!(pszDst = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cchLen + 1)))
        return NULL;

    StringCchCopyA(pszDst, cchLen + 1, pszSrc);
    return pszDst;
}

static LPSTR TrimWhitespace(IN LPSTR pszStr)
{
    LPSTR pszEnd = NULL;

    if (!pszStr)
        return NULL;

    while (*pszStr == ' ' || *pszStr == '\t' || *pszStr == '\n' || *pszStr == '\r')
        pszStr++;

    if (*pszStr == '\0')
        return pszStr;

    pszEnd = pszStr + lstrlenA(pszStr) - 1;
    while (pszEnd > pszStr && (*pszEnd == ' ' || *pszEnd == '\t' || *pszEnd == '\n' || *pszEnd == '\r'))
        pszEnd--;

    pszEnd[1] = '\0';
    return pszStr;
}

static LPCSTR SkipWhitespace(IN LPCSTR psz)
{
    if (!psz)
        return NULL;
    while (*psz == ' ' || *psz == '\t' || *psz == '\n' || *psz == '\r')
        psz++;
    return psz;
}

#pragma endregion

// ===============================================================================================================================================================
// CELL AND RECORD PARSING
// ===============================================================================================================================================================

#pragma region CELL_AND_RECORD_PARSING

static BOOL ParseCell(IN PSQLOOT_DB pDb, IN PBYTE pbPage, IN DWORD dwCellOffset, IN BYTE bPageType, IN BOOL bIsFirstPage, OUT PSQLOOT_CELL pCell)
{
    PBYTE   pbCell          = NULL;
    DWORD   dwOffset        = 0;
    INT64   i64PayloadSize  = 0;
    INT64   i64RowId        = 0;
    DWORD   dwVarintLen     = 0;
    DWORD   dwUsableSize    = 0;
    DWORD   dwMaxLocal      = 0;
    DWORD   dwMinLocal      = 0;
    DWORD   dwLocalSize     = 0;
    DWORD   dwOverflowPage  = 0;

    if (!pDb || !pbPage || !pCell)
        return FALSE;

    ZeroMemory(pCell, sizeof(SQLOOT_CELL));
    pbCell = pbPage + dwCellOffset;

    dwUsableSize = pDb->dwPageSize;

    switch (bPageType)
    {
        case SQLOOT_PAGE_LEAF_TABLE:
            dwVarintLen = ReadVarint(pbCell + dwOffset, &i64PayloadSize);
            dwOffset += dwVarintLen;
            dwVarintLen = ReadVarint(pbCell + dwOffset, &i64RowId);
            dwOffset += dwVarintLen;

            pCell->i64RowId = i64RowId;
            pCell->dwPayloadSize = (DWORD)i64PayloadSize;

            dwMaxLocal = dwUsableSize - 35;
            dwMinLocal = (dwUsableSize - 12) * 32 / 255 - 23;
            if (dwMinLocal < 1) dwMinLocal = 1;

            if ((DWORD)i64PayloadSize <= dwMaxLocal)
            {
                pCell->pbPayload = pbCell + dwOffset;
                pCell->dwLeftChildPage = 0;
            }
            else
            {
                DWORD dwActualSize = 0;
                dwLocalSize = dwMinLocal + ((DWORD)i64PayloadSize - dwMinLocal) % (dwUsableSize - 4);
                if (dwLocalSize > dwMaxLocal)
                    dwLocalSize = dwMinLocal;

                dwOverflowPage = ReadBigEndian32(pbCell + dwOffset + dwLocalSize);

                pCell->pbPayload = ReadPayloadWithOverflow(pDb, pbCell + dwOffset, dwLocalSize, (DWORD)i64PayloadSize, dwOverflowPage, &dwActualSize);
                pCell->dwPayloadSize = dwActualSize;
                pCell->dwLeftChildPage = 0;
            }
            break;

        case SQLOOT_PAGE_INTERIOR_TABLE:
            pCell->dwLeftChildPage = ReadBigEndian32(pbCell);
            dwOffset += 4;
            dwVarintLen = ReadVarint(pbCell + dwOffset, &i64RowId);
            dwOffset += dwVarintLen;
            pCell->i64RowId         = i64RowId;
            pCell->pbPayload        = NULL;
            pCell->dwPayloadSize    = 0;
            break;

        case SQLOOT_PAGE_LEAF_INDEX:
        {
            DWORD dwActualSize = 0;
            dwVarintLen = ReadVarint(pbCell + dwOffset, &i64PayloadSize);
            dwOffset += dwVarintLen;

            dwMaxLocal = (dwUsableSize - 12) * 64 / 255 - 23;
            dwMinLocal = (dwUsableSize - 12) * 32 / 255 - 23;
            if (dwMinLocal < 1) dwMinLocal = 1;

            if ((DWORD)i64PayloadSize <= dwMaxLocal)
            {
                pCell->pbPayload = pbCell + dwOffset;
                pCell->dwPayloadSize = (DWORD)i64PayloadSize;
            }
            else
            {
                dwLocalSize = dwMinLocal + ((DWORD)i64PayloadSize - dwMinLocal) % (dwUsableSize - 4);
                if (dwLocalSize > dwMaxLocal)
                    dwLocalSize = dwMinLocal;

                dwOverflowPage = ReadBigEndian32(pbCell + dwOffset + dwLocalSize);

                pCell->pbPayload = ReadPayloadWithOverflow(pDb, pbCell + dwOffset, dwLocalSize, (DWORD)i64PayloadSize, dwOverflowPage, &dwActualSize);
                pCell->dwPayloadSize = dwActualSize;
            }
            pCell->i64RowId = 0;
            pCell->dwLeftChildPage = 0;
            break;
        }

        case SQLOOT_PAGE_INTERIOR_INDEX:
        {
            DWORD dwActualSize = 0;
            pCell->dwLeftChildPage = ReadBigEndian32(pbCell);
            dwOffset += 4;
            dwVarintLen = ReadVarint(pbCell + dwOffset, &i64PayloadSize);
            dwOffset += dwVarintLen;

            dwMaxLocal = (dwUsableSize - 12) * 64 / 255 - 23;
            dwMinLocal = (dwUsableSize - 12) * 32 / 255 - 23;
            if (dwMinLocal < 1) dwMinLocal = 1;

            if ((DWORD)i64PayloadSize <= dwMaxLocal)
            {
                pCell->pbPayload = pbCell + dwOffset;
                pCell->dwPayloadSize = (DWORD)i64PayloadSize;
            }
            else
            {
                dwLocalSize = dwMinLocal + ((DWORD)i64PayloadSize - dwMinLocal) % (dwUsableSize - 4);
                if (dwLocalSize > dwMaxLocal)
                    dwLocalSize = dwMinLocal;

                dwOverflowPage = ReadBigEndian32(pbCell + dwOffset + dwLocalSize);

                pCell->pbPayload = ReadPayloadWithOverflow(pDb, pbCell + dwOffset, dwLocalSize, (DWORD)i64PayloadSize, dwOverflowPage, &dwActualSize);
                pCell->dwPayloadSize = dwActualSize;
            }
            pCell->i64RowId = 0;
            break;
        }

        default:
            return FALSE;
    }

    return TRUE;
}

static BOOL ParseRecord(IN PBYTE pbPayload, IN DWORD dwPayloadSize, OUT PSQLOOT_ROW pRow)
{
    DWORD   dwOffset        = 0;
    INT64   i64HeaderSize   = 0;
    DWORD   dwHeaderLen     = 0;
    DWORD   dwHeaderEnd     = 0;
    DWORD   dwDataOffset    = 0;
    DWORD   dwColIndex      = 0;
    INT64   i64SerialType   = 0;
    DWORD   dwVarintLen     = 0;
    DWORD   dwColCount      = 0;
    INT64   ai64SerialTypes[SQLOOT_MAX_COLUMNS] = { 0 };

    if (!pbPayload || dwPayloadSize == 0 || !pRow)
        return FALSE;

    ZeroMemory(pRow, sizeof(SQLOOT_ROW));

    dwHeaderLen = ReadVarint(pbPayload, &i64HeaderSize);
    dwOffset = dwHeaderLen;
    dwHeaderEnd = (DWORD)i64HeaderSize;
    dwDataOffset = dwHeaderEnd;

    while (dwOffset < dwHeaderEnd && dwColCount < SQLOOT_MAX_COLUMNS)
    {
        dwVarintLen = ReadVarint(pbPayload + dwOffset, &i64SerialType);
        dwOffset += dwVarintLen;
        ai64SerialTypes[dwColCount++] = i64SerialType;
    }

    if (dwColCount == 0)
        return FALSE;

    pRow->pColumns = (PSQLOOT_COLUMN)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwColCount * sizeof(SQLOOT_COLUMN));
    if (!pRow->pColumns)
        return FALSE;

    pRow->dwColumnCount = dwColCount;

    for (dwColIndex = 0; dwColIndex < dwColCount; dwColIndex++)
    {
        PSQLOOT_VALUE   pValue      = &pRow->pColumns[dwColIndex].Value;
        INT64           i64Serial   = ai64SerialTypes[dwColIndex];

        if (i64Serial == 0)
        {
            pValue->nType = SQLOOT_TYPE_NULL;
        }
        else if (i64Serial == 1)
        {
            pValue->nType = SQLOOT_TYPE_INT8;
            pValue->i64Value = (INT8)pbPayload[dwDataOffset];
            dwDataOffset += 1;
        }
        else if (i64Serial == 2)
        {
            pValue->nType = SQLOOT_TYPE_INT16;
            pValue->i64Value = (INT16)ReadBigEndian16(pbPayload + dwDataOffset);
            dwDataOffset += 2;
        }
        else if (i64Serial == 3)
        {
            pValue->nType = SQLOOT_TYPE_INT24;
            pValue->i64Value = ReadBigEndianInt(pbPayload + dwDataOffset, 3);
            dwDataOffset += 3;
        }
        else if (i64Serial == 4)
        {
            pValue->nType = SQLOOT_TYPE_INT32;
            pValue->i64Value = (INT32)ReadBigEndian32(pbPayload + dwDataOffset);
            dwDataOffset += 4;
        }
        else if (i64Serial == 5)
        {
            pValue->nType = SQLOOT_TYPE_INT48;
            pValue->i64Value = ReadBigEndianInt(pbPayload + dwDataOffset, 6);
            dwDataOffset += 6;
        }
        else if (i64Serial == 6)
        {
            pValue->nType = SQLOOT_TYPE_INT64;
            pValue->i64Value = ReadBigEndianInt(pbPayload + dwDataOffset, 8);
            dwDataOffset += 8;
        }
        else if (i64Serial == 7)
        {
            BYTE abTemp[8] = { 0 };
            INT i = 0;
            pValue->nType = SQLOOT_TYPE_FLOAT64;
            for (i = 0; i < 8; i++)
                abTemp[i] = pbPayload[dwDataOffset + 7 - i];
            CopyMemory(&pValue->dValue, abTemp, 8);
            dwDataOffset += 8;
        }
        else if (i64Serial == 8)
        {
            pValue->nType = SQLOOT_TYPE_ZERO;
            pValue->i64Value = 0;
        }
        else if (i64Serial == 9)
        {
            pValue->nType = SQLOOT_TYPE_ONE;
            pValue->i64Value = 1;
        }
        else if (i64Serial >= 12 && (i64Serial % 2) == 0)
        {
            DWORD dwBlobSize = (DWORD)((i64Serial - 12) / 2);
            pValue->nType = SQLOOT_TYPE_BLOB;
            pValue->Blob.dwSize = dwBlobSize;
            if (dwBlobSize > 0)
            {
                pValue->Blob.pbData = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwBlobSize);
                if (pValue->Blob.pbData)
                    CopyMemory(pValue->Blob.pbData, pbPayload + dwDataOffset, dwBlobSize);
            }
            dwDataOffset += dwBlobSize;
        }
        else if (i64Serial >= 13 && (i64Serial % 2) == 1)
        {
            DWORD dwTextSize = (DWORD)((i64Serial - 13) / 2);
            pValue->nType = SQLOOT_TYPE_TEXT;
            pValue->Text.dwLength = dwTextSize;
            if (dwTextSize > 0)
            {
                pValue->Text.pszData = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwTextSize + 1);
                if (pValue->Text.pszData)
                    CopyMemory(pValue->Text.pszData, pbPayload + dwDataOffset, dwTextSize);
            }
            else
            {
                pValue->Text.pszData = DuplicateStringA("");
            }
            dwDataOffset += dwTextSize;
        }
    }

    return TRUE;
}

static VOID FreeRow(IN PSQLOOT_ROW pRow)
{
    DWORD i = 0;

    if (!pRow)
        return;

    if (pRow->pColumns)
    {
        for (i = 0; i < pRow->dwColumnCount; i++)
        {
            PSQLOOT_VALUE pValue = &pRow->pColumns[i].Value;
            if (pValue->nType == SQLOOT_TYPE_BLOB && pValue->Blob.pbData)
                HeapFree(GetProcessHeap(), 0, pValue->Blob.pbData);
            else if (pValue->nType == SQLOOT_TYPE_TEXT && pValue->Text.pszData)
                HeapFree(GetProcessHeap(), 0, pValue->Text.pszData);
        }
        HeapFree(GetProcessHeap(), 0, pRow->pColumns);
    }

    ZeroMemory(pRow, sizeof(SQLOOT_ROW));
}

static VOID FreeExpression(IN PSQLOOT_EXPR pExpr)
{
    if (!pExpr)
        return;

    if (pExpr->pLeft)
        FreeExpression(pExpr->pLeft);
    if (pExpr->pRight)
        FreeExpression(pExpr->pRight);
    if (pExpr->pFuncArg)
        FreeExpression(pExpr->pFuncArg);

    HeapFree(GetProcessHeap(), 0, pExpr);
}

#pragma endregion

// ===============================================================================================================================================================
// SCHEMA PARSING
// ===============================================================================================================================================================

#pragma region SCHEMA_PARSING

static BOOL ParseSchema(IN PSQLOOT_DB pDb)
{
    SQLOOT_TABLE_REF    SchemaRef       = { 0 };
    PSQLOOT_TABLE_INFO  pTables         = NULL;
    DWORD               dwTableCount    = 0;
    DWORD               dwTableCapacity = 0;

    if (!pDb || !pDb->pbFileData)
        return FALSE;

    // Use the table iterator to traverse the full sqlite_master B-tree
    SchemaRef.dwRootPage = SQLOOT_MASTER_ROOT_PAGE;

    if (!InitTableIterator(pDb, &SchemaRef))
    {
        SQLOOT_SET_ERROR("Failed To Initialize Schema Iterator [Line:%d]", __LINE__);
        return FALSE;
    }

    dwTableCapacity = 64;
    pTables = (PSQLOOT_TABLE_INFO)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwTableCapacity * sizeof(SQLOOT_TABLE_INFO));
    if (!pTables)
    {
        SQLOOT_SET_ERROR("HeapAlloc Failed With Error: %lu [Line:%d]", GetLastError(), __LINE__);
        FreeTableRef(&SchemaRef);
        return FALSE;
    }

    while (NextTableRow(pDb, &SchemaRef))
    {
        PSQLOOT_ROW pRow = &SchemaRef.CurrentRow;

        if (pRow->dwColumnCount < 5)
            continue;

        // Grow buffer if needed
        if (dwTableCount >= dwTableCapacity)
        {
            DWORD               dwNewCapacity   = dwTableCapacity * 2;
            PSQLOOT_TABLE_INFO  pNewTables      = (PSQLOOT_TABLE_INFO)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwNewCapacity * sizeof(SQLOOT_TABLE_INFO));

            if (!pNewTables)
                break;

            CopyMemory(pNewTables, pTables, dwTableCount * sizeof(SQLOOT_TABLE_INFO));
            HeapFree(GetProcessHeap(), 0, pTables);
            pTables = pNewTables;
            dwTableCapacity = dwNewCapacity;
        }

        {
            PSQLOOT_TABLE_INFO pInfo = &pTables[dwTableCount];

            if (pRow->pColumns[0].Value.nType == SQLOOT_TYPE_TEXT && pRow->pColumns[0].Value.Text.pszData)
                StringCchCopyA(pInfo->szType, ARRAYSIZE(pInfo->szType), pRow->pColumns[0].Value.Text.pszData);

            if (pRow->pColumns[1].Value.nType == SQLOOT_TYPE_TEXT && pRow->pColumns[1].Value.Text.pszData)
                StringCchCopyA(pInfo->szName, ARRAYSIZE(pInfo->szName), pRow->pColumns[1].Value.Text.pszData);

            if (pRow->pColumns[2].Value.nType == SQLOOT_TYPE_TEXT && pRow->pColumns[2].Value.Text.pszData)
                StringCchCopyA(pInfo->szTableName, ARRAYSIZE(pInfo->szTableName), pRow->pColumns[2].Value.Text.pszData);

            if (pRow->pColumns[3].Value.nType >= SQLOOT_TYPE_INT8 && pRow->pColumns[3].Value.nType <= SQLOOT_TYPE_ONE)
                pInfo->dwRootPage = (DWORD)pRow->pColumns[3].Value.i64Value;

            if (pRow->pColumns[4].Value.nType == SQLOOT_TYPE_TEXT && pRow->pColumns[4].Value.Text.pszData)
                pInfo->pszSql = DuplicateStringA(pRow->pColumns[4].Value.Text.pszData);

            ParseCreateTableColumns(pInfo);
            dwTableCount++;
        }
    }

    FreeTableRef(&SchemaRef);

    pDb->pTables = pTables;
    pDb->dwTableCount = dwTableCount;

    return TRUE;
}
#pragma endregion

// ===============================================================================================================================================================
// TABLE ITERATOR
// ===============================================================================================================================================================

#pragma region TABLE_ITERATOR

static BOOL InitTableIterator(IN PSQLOOT_DB pDb, IN OUT PSQLOOT_TABLE_REF pTableRef)
{
    if (!pDb || !pTableRef || pTableRef->dwRootPage == 0)
        return FALSE;

    pTableRef->dwStackCapacity = 32;
    pTableRef->pdwPageStack = (PDWORD)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pTableRef->dwStackCapacity * sizeof(DWORD));
    pTableRef->pdwCellIndexStack = (PDWORD)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pTableRef->dwStackCapacity * sizeof(DWORD));

    if (!pTableRef->pdwPageStack || !pTableRef->pdwCellIndexStack)
        return FALSE;

    pTableRef->pdwPageStack[0] = pTableRef->dwRootPage;
    pTableRef->pdwCellIndexStack[0] = 0;
    pTableRef->dwStackDepth = 1;
    pTableRef->bHasRow = FALSE;
    pTableRef->bEndOfTable = FALSE;

    return TRUE;
}

static VOID ResetTableIterator(IN OUT PSQLOOT_TABLE_REF pTableRef)
{
    if (!pTableRef)
        return;

    FreeRow(&pTableRef->CurrentRow);

    if (pTableRef->pdwPageStack)
    {
        ZeroMemory(pTableRef->pdwPageStack, pTableRef->dwStackCapacity * sizeof(DWORD));
        pTableRef->pdwPageStack[0] = pTableRef->dwRootPage;
    }

    if (pTableRef->pdwCellIndexStack)
    {
        ZeroMemory(pTableRef->pdwCellIndexStack, pTableRef->dwStackCapacity * sizeof(DWORD));
    }

    pTableRef->dwStackDepth = 1;
    pTableRef->bHasRow = FALSE;
    pTableRef->bEndOfTable = FALSE;
}

static VOID FreeTableRef(IN OUT PSQLOOT_TABLE_REF pTableRef)
{
    if (!pTableRef)
        return;

    FreeRow(&pTableRef->CurrentRow);

    if (pTableRef->pdwPageStack)
    {
        HeapFree(GetProcessHeap(), 0, pTableRef->pdwPageStack);
        pTableRef->pdwPageStack = NULL;
    }

    if (pTableRef->pdwCellIndexStack)
    {
        HeapFree(GetProcessHeap(), 0, pTableRef->pdwCellIndexStack);
        pTableRef->pdwCellIndexStack = NULL;
    }
}

static BOOL NextTableRow(IN PSQLOOT_DB pDb, IN OUT PSQLOOT_TABLE_REF pTableRef)
{
    PBYTE               pbPage          = NULL;
    SQLOOT_PAGE_INFO    PageInfo        = { 0 };
    SQLOOT_CELL         Cell            = { 0 };
    WORD                wCellOffset     = 0;
    PBYTE               pbCellPointers  = NULL;
    DWORD               dwCurrentPage   = 0;
    DWORD               dwCellIndex     = 0;
    BOOL                bIsFirstPage    = FALSE;

    if (!pDb || !pTableRef)
        return FALSE;

    if (pTableRef->bEndOfTable)
        return FALSE;

    FreeRow(&pTableRef->CurrentRow);
    pTableRef->bHasRow = FALSE;

    while (pTableRef->dwStackDepth > 0)
    {
        dwCurrentPage = pTableRef->pdwPageStack[pTableRef->dwStackDepth - 1];
        dwCellIndex = pTableRef->pdwCellIndexStack[pTableRef->dwStackDepth - 1];
        bIsFirstPage = (dwCurrentPage == 1);

        pbPage = GetPage(pDb, dwCurrentPage);
        if (!pbPage)
        {
            pTableRef->dwStackDepth--;
            continue;
        }

        if (!ParsePageInfo(pbPage, pDb->dwPageSize, bIsFirstPage, &PageInfo))
        {
            pTableRef->dwStackDepth--;
            continue;
        }

        pbCellPointers = pbPage + (bIsFirstPage ? SQLOOT_HEADER_SIZE : 0);
        if (PageInfo.bPageType == SQLOOT_PAGE_INTERIOR_TABLE || PageInfo.bPageType == SQLOOT_PAGE_INTERIOR_INDEX)
            pbCellPointers += 12;
        else
            pbCellPointers += 8;

        if (PageInfo.bPageType == SQLOOT_PAGE_INTERIOR_TABLE)
        {
            if (dwCellIndex < PageInfo.wCellCount)
            {
                wCellOffset = ReadBigEndian16(pbCellPointers + (dwCellIndex * 2));

                if (!ParseCell(pDb, pbPage, wCellOffset, PageInfo.bPageType, bIsFirstPage, &Cell))
                {
                    pTableRef->pdwCellIndexStack[pTableRef->dwStackDepth - 1]++;
                    continue;
                }

                pTableRef->pdwCellIndexStack[pTableRef->dwStackDepth - 1]++;

                if (Cell.dwLeftChildPage > 0 && pTableRef->dwStackDepth < pTableRef->dwStackCapacity)
                {
                    pTableRef->pdwPageStack[pTableRef->dwStackDepth] = Cell.dwLeftChildPage;
                    pTableRef->pdwCellIndexStack[pTableRef->dwStackDepth] = 0;
                    pTableRef->dwStackDepth++;
                }
                continue;
            }
            else if (dwCellIndex == PageInfo.wCellCount && PageInfo.dwRightMostPointer > 0)
            {
                pTableRef->pdwCellIndexStack[pTableRef->dwStackDepth - 1]++;

                if (pTableRef->dwStackDepth < pTableRef->dwStackCapacity)
                {
                    pTableRef->pdwPageStack[pTableRef->dwStackDepth] = PageInfo.dwRightMostPointer;
                    pTableRef->pdwCellIndexStack[pTableRef->dwStackDepth] = 0;
                    pTableRef->dwStackDepth++;
                }
                continue;
            }
            else
            {
                pTableRef->dwStackDepth--;
                continue;
            }
        }
        else if (PageInfo.bPageType == SQLOOT_PAGE_LEAF_TABLE)
        {
            if (dwCellIndex < PageInfo.wCellCount)
            {
                wCellOffset = ReadBigEndian16(pbCellPointers + (dwCellIndex * 2));
                pTableRef->pdwCellIndexStack[pTableRef->dwStackDepth - 1]++;

                if (!ParseCell(pDb, pbPage, wCellOffset, PageInfo.bPageType, bIsFirstPage, &Cell))
                    continue;

                if (!ParseRecord(Cell.pbPayload, Cell.dwPayloadSize, &pTableRef->CurrentRow))
                    continue;

                pTableRef->CurrentRow.i64RowId = Cell.i64RowId;
                pTableRef->bHasRow = TRUE;
                return TRUE;
            }
            else
            {
                pTableRef->dwStackDepth--;
                continue;
            }
        }
        else
        {
            pTableRef->dwStackDepth--;
            continue;
        }
    }

    pTableRef->bEndOfTable = TRUE;
    return FALSE;
}

static BOOL SeekTableRowById(IN PSQLOOT_DB pDb, IN OUT PSQLOOT_TABLE_REF pTableRef, IN INT64 i64TargetRowId)
{
    DWORD   dwCurrentPage = 0;

    if (!pDb || !pTableRef || pTableRef->dwRootPage == 0)
        return FALSE;

    FreeRow(&pTableRef->CurrentRow);
    pTableRef->bHasRow = FALSE;

    dwCurrentPage = pTableRef->dwRootPage;

    while (dwCurrentPage != 0 && dwCurrentPage <= pDb->dwPageCount)
    {
        PBYTE               pbPage          = NULL;
        SQLOOT_PAGE_INFO    PageInfo        = { 0 };
        PBYTE               pbCellPointers  = NULL;
        BOOL                bIsFirstPage    = (dwCurrentPage == 1);
        DWORD               i               = 0;

        pbPage = GetPage(pDb, dwCurrentPage);
        if (!pbPage)
            return FALSE;

        if (!ParsePageInfo(pbPage, pDb->dwPageSize, bIsFirstPage, &PageInfo))
            return FALSE;

        pbCellPointers = pbPage + (bIsFirstPage ? SQLOOT_HEADER_SIZE : 0);

        if (PageInfo.bPageType == SQLOOT_PAGE_INTERIOR_TABLE)
        {
            BOOL bFoundChild = FALSE;
            pbCellPointers += 12;

            // Interior page cells are sorted by rowid.
            // Each cell has: [left-child-page (4 bytes)] [rowid (varint)]
            // Invariant: all rows in left subtree have rowid <= cell's rowid.
            // Find first cell where target <= cell.rowid, then descend into left child.
            for (i = 0; i < PageInfo.wCellCount; i++)
            {
                WORD        wCellOffset = ReadBigEndian16(pbCellPointers + (i * 2));
                SQLOOT_CELL Cell        = { 0 };

                if (!ParseCell(pDb, pbPage, wCellOffset, PageInfo.bPageType, bIsFirstPage, &Cell))
                    continue;

                if (i64TargetRowId <= Cell.i64RowId)
                {
                    dwCurrentPage = Cell.dwLeftChildPage;
                    bFoundChild = TRUE;
                    break;
                }
            }

            if (!bFoundChild)
            {
                // Target is larger than all keys — go to rightmost pointer
                dwCurrentPage = PageInfo.dwRightMostPointer;
            }
        }
        else if (PageInfo.bPageType == SQLOOT_PAGE_LEAF_TABLE)
        {
            pbCellPointers += 8;

            // Leaf page cells are sorted by rowid. Linear scan within this single page.
            for (i = 0; i < PageInfo.wCellCount; i++)
            {
                WORD        wCellOffset = ReadBigEndian16(pbCellPointers + (i * 2));
                SQLOOT_CELL Cell        = { 0 };

                if (!ParseCell(pDb, pbPage, wCellOffset, PageInfo.bPageType, bIsFirstPage, &Cell))
                    continue;

                if (Cell.i64RowId == i64TargetRowId)
                {
                    if (!ParseRecord(Cell.pbPayload, Cell.dwPayloadSize, &pTableRef->CurrentRow))
                        return FALSE;

                    pTableRef->CurrentRow.i64RowId = Cell.i64RowId;
                    pTableRef->bHasRow = TRUE;
                    return TRUE;
                }

                // Cells are sorted — if we've passed the target, it doesn't exist
                if (Cell.i64RowId > i64TargetRowId)
                    return FALSE;
            }

            return FALSE;
        }
        else
        {
            return FALSE;
        }
    }

    return FALSE;
}

#pragma endregion

// ===============================================================================================================================================================
// SQL PARSING
// ===============================================================================================================================================================

#pragma region SQL_PARSING

static BOOL ParseToken(IN OUT LPCSTR* ppszSql, OUT LPSTR pszToken, IN DWORD dwMaxLen)
{
    LPCSTR  psz     = NULL;
    DWORD   dwLen   = 0;

    if (!ppszSql || !*ppszSql || !pszToken || dwMaxLen == 0)
        return FALSE;

    pszToken[0] = '\0';
    psz = SkipWhitespace(*ppszSql);

    if (*psz == '\0')
        return FALSE;

    // Handle quoted strings
    if (*psz == '\'' || *psz == '"')
    {
        CHAR cQuote = *psz;
        psz++;
        while (*psz && *psz != cQuote && dwLen < dwMaxLen - 1)
        {
            pszToken[dwLen++] = *psz++;
        }
        if (*psz == cQuote)
            psz++;
        pszToken[dwLen] = '\0';
        *ppszSql = psz;
        return TRUE;
    }

    // Handle operators
    if (*psz == '=' || *psz == '<' || *psz == '>' || *psz == '!' ||
        *psz == '(' || *psz == ')' || *psz == ',' || *psz == ';')
    {
        pszToken[0] = *psz++;
        pszToken[1] = '\0';

        // Check for two-char operators
        if ((pszToken[0] == '<' || pszToken[0] == '>' || pszToken[0] == '!') && *psz == '=')
        {
            pszToken[1] = *psz++;
            pszToken[2] = '\0';
        }
        else if (pszToken[0] == '<' && *psz == '>')
        {
            pszToken[1] = *psz++;
            pszToken[2] = '\0';
        }

        *ppszSql = psz;
        return TRUE;
    }

    // Handle identifiers and keywords
    while (*psz && *psz != ' ' && *psz != '\t' && *psz != '\n' && *psz != '\r' &&
           *psz != '=' && *psz != '<' && *psz != '>' && *psz != '!' &&
           *psz != '(' && *psz != ')' && *psz != ',' && *psz != ';' &&
           dwLen < dwMaxLen - 1)
    {
        pszToken[dwLen++] = *psz++;
    }

    pszToken[dwLen] = '\0';
    *ppszSql = psz;
    return dwLen > 0;
}

static BOOL ParseColumnRef(IN LPCSTR pszToken, OUT PSQLOOT_COLUMN_REF pColRef)
{
    LPCSTR pszDot = NULL;

    if (!pszToken || !pColRef)
        return FALSE;

    ZeroMemory(pColRef, sizeof(SQLOOT_COLUMN_REF));
    pColRef->nTableIndex = -1;
    pColRef->nColumnIndex = -1;

    pszDot = StrChrA(pszToken, '.');

    if (pszDot)
    {
        SIZE_T cchAlias = pszDot - pszToken;
        if (cchAlias >= SQLOOT_MAX_TABLE_NAME)
            cchAlias = SQLOOT_MAX_TABLE_NAME - 1;

        StringCchCopyNA(pColRef->szAlias, ARRAYSIZE(pColRef->szAlias), pszToken, cchAlias);
        StringCchCopyA(pColRef->szColumn, ARRAYSIZE(pColRef->szColumn), pszDot + 1);
    }
    else
    {
        StringCchCopyA(pColRef->szColumn, ARRAYSIZE(pColRef->szColumn), pszToken);
    }

    return TRUE;
}

static PSQLOOT_EXPR ParsePrimaryExpression(IN LPCSTR* ppszSql)
{
    PSQLOOT_EXPR    pExpr       = NULL;
    CHAR            szToken[512] = { 0 };
    LPCSTR          pszSaved    = NULL;

    if (!ppszSql || !*ppszSql)
        return NULL;

    *ppszSql = SkipWhitespace(*ppszSql);
    pszSaved = *ppszSql;

    if (!ParseToken(ppszSql, szToken, ARRAYSIZE(szToken)))
        return NULL;

    // Handle parentheses
    if (StrCmpIA(szToken, "(") == 0)
    {
        pExpr = ParseAndExpression(ppszSql);
        if (pExpr)
        {
            ParseToken(ppszSql, szToken, ARRAYSIZE(szToken)); // consume ')'
        }
        return pExpr;
    }

    // Handle NULL
    if (StrCmpIA(szToken, "NULL") == 0)
    {
        pExpr = (PSQLOOT_EXPR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_EXPR));
        if (pExpr)
            pExpr->nType = SQLOOT_EXPR_NULL;
        return pExpr;
    }

    // Handle length() function
    if (StrCmpIA(szToken, "length") == 0)
    {
        *ppszSql = SkipWhitespace(*ppszSql);
        if (**ppszSql == '(')
        {
            (*ppszSql)++; // consume '('

            pExpr = (PSQLOOT_EXPR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_EXPR));
            if (pExpr)
            {
                pExpr->nType = SQLOOT_EXPR_FUNCTION;
                pExpr->nFuncType = SQLOOT_FUNC_LENGTH;
                pExpr->pFuncArg = ParseAndExpression(ppszSql);

                // Consume closing ')'
                *ppszSql = SkipWhitespace(*ppszSql);
                if (**ppszSql == ')')
                    (*ppszSql)++;
            }
            return pExpr;
        }
        // Not a function call, treat as column name
    }

    // Handle numbers
    if ((szToken[0] >= '0' && szToken[0] <= '9') ||
        (szToken[0] == '-' && szToken[1] >= '0' && szToken[1] <= '9'))
    {
        pExpr = (PSQLOOT_EXPR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_EXPR));
        if (pExpr)
        {
            if (StrChrA(szToken, '.'))
            {
                pExpr->nType = SQLOOT_EXPR_FLOAT;
                pExpr->dValue = atof(szToken);
            }
            else
            {
                pExpr->nType = SQLOOT_EXPR_INTEGER;
                pExpr->i64Value = _atoi64(szToken);
            }
        }
        return pExpr;
    }

    // Handle string literals (if we got here from quoted string)
    if (pszSaved[0] == '\'' || pszSaved[0] == '"')
    {
        pExpr = (PSQLOOT_EXPR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_EXPR));
        if (pExpr)
        {
            pExpr->nType = SQLOOT_EXPR_STRING;
            StringCchCopyA(pExpr->szStrValue, ARRAYSIZE(pExpr->szStrValue), szToken);
        }
        return pExpr;
    }

    // Must be a column reference
    pExpr = (PSQLOOT_EXPR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_EXPR));
    if (pExpr)
    {
        pExpr->nType = SQLOOT_EXPR_COLUMN;
        ParseColumnRef(szToken, &pExpr->ColRef);
    }

    return pExpr;
}

static PSQLOOT_EXPR ParseCompareExpression(IN LPCSTR* ppszSql)
{
    PSQLOOT_EXPR    pLeft       = NULL;
    PSQLOOT_EXPR    pRight      = NULL;
    PSQLOOT_EXPR    pExpr       = NULL;
    CHAR            szToken[64] = { 0 };
    LPCSTR          pszSaved    = NULL;
    INT             nOperator   = 0;

    if (!ppszSql || !*ppszSql)
        return NULL;

    pLeft = ParsePrimaryExpression(ppszSql);
    if (!pLeft)
        return NULL;

    *ppszSql = SkipWhitespace(*ppszSql);
    pszSaved = *ppszSql;

    if (!ParseToken(ppszSql, szToken, ARRAYSIZE(szToken)))
        return pLeft;

    // Check for IS NULL / IS NOT NULL
    if (StrCmpIA(szToken, "IS") == 0)
    {
        ParseToken(ppszSql, szToken, ARRAYSIZE(szToken));

        if (StrCmpIA(szToken, "NOT") == 0)
        {
            ParseToken(ppszSql, szToken, ARRAYSIZE(szToken)); // consume NULL
            pExpr = (PSQLOOT_EXPR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_EXPR));
            if (pExpr)
            {
                pExpr->nType = SQLOOT_EXPR_IS_NOT_NULL;
                pExpr->pLeft = pLeft;
            }
            return pExpr;
        }
        else if (StrCmpIA(szToken, "NULL") == 0)
        {
            pExpr = (PSQLOOT_EXPR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_EXPR));
            if (pExpr)
            {
                pExpr->nType = SQLOOT_EXPR_IS_NULL;
                pExpr->pLeft = pLeft;
            }
            return pExpr;
        }

        *ppszSql = pszSaved;
        return pLeft;
    }

    // Check for comparison operators
    if (StrCmpIA(szToken, "=") == 0)
        nOperator = SQLOOT_CMP_EQ;
    else if (StrCmpIA(szToken, "!=") == 0 || StrCmpIA(szToken, "<>") == 0)
        nOperator = SQLOOT_CMP_NE;
    else if (StrCmpIA(szToken, "<") == 0)
        nOperator = SQLOOT_CMP_LT;
    else if (StrCmpIA(szToken, "<=") == 0)
        nOperator = SQLOOT_CMP_LE;
    else if (StrCmpIA(szToken, ">") == 0)
        nOperator = SQLOOT_CMP_GT;
    else if (StrCmpIA(szToken, ">=") == 0)
        nOperator = SQLOOT_CMP_GE;
    else
    {
        *ppszSql = pszSaved;
        return pLeft;
    }

    pRight = ParsePrimaryExpression(ppszSql);
    if (!pRight)
    {
        FreeExpression(pLeft);
        return NULL;
    }

    pExpr = (PSQLOOT_EXPR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_EXPR));
    if (!pExpr)
    {
        FreeExpression(pLeft);
        FreeExpression(pRight);
        return NULL;
    }

    pExpr->nType = SQLOOT_EXPR_COMPARE;
    pExpr->nOperator = nOperator;
    pExpr->pLeft = pLeft;
    pExpr->pRight = pRight;

    return pExpr;
}

static PSQLOOT_EXPR ParseAndExpression(IN LPCSTR* ppszSql)
{
    PSQLOOT_EXPR    pLeft       = NULL;
    PSQLOOT_EXPR    pRight      = NULL;
    PSQLOOT_EXPR    pExpr       = NULL;
    CHAR            szToken[64] = { 0 };
    LPCSTR          pszSaved    = NULL;

    if (!ppszSql || !*ppszSql)
        return NULL;

    pLeft = ParseCompareExpression(ppszSql);
    if (!pLeft)
        return NULL;

    while (TRUE)
    {
        *ppszSql = SkipWhitespace(*ppszSql);
        pszSaved = *ppszSql;

        if (!ParseToken(ppszSql, szToken, ARRAYSIZE(szToken)))
            break;

        if (StrCmpIA(szToken, "AND") != 0)
        {
            *ppszSql = pszSaved;
            break;
        }

        pRight = ParseCompareExpression(ppszSql);
        if (!pRight)
        {
            FreeExpression(pLeft);
            return NULL;
        }

        pExpr = (PSQLOOT_EXPR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_EXPR));
        if (!pExpr)
        {
            FreeExpression(pLeft);
            FreeExpression(pRight);
            return NULL;
        }

        pExpr->nType = SQLOOT_EXPR_AND;
        pExpr->pLeft = pLeft;
        pExpr->pRight = pRight;
        pLeft = pExpr;
    }

    return pLeft;
}

static PSQLOOT_EXPR ParseExpression(IN LPCSTR* ppszSql)
{
    return ParseAndExpression(ppszSql);
}

static BOOL FindTableByNameOrAlias(IN PSQLOOT_STMT pStmt, IN LPCSTR pszName, OUT PINT pnIndex)
{
    DWORD i = 0;

    if (!pStmt || !pszName || !pnIndex)
        return FALSE;

    *pnIndex = -1;

    for (i = 0; i < pStmt->dwTableCount; i++)
    {
        if ((pStmt->Tables[i].szAlias[0] && StrCmpIA(pStmt->Tables[i].szAlias, pszName) == 0) ||
            StrCmpIA(pStmt->Tables[i].szTableName, pszName) == 0)
        {
            *pnIndex = (INT)i;
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL IsColumnRowId(IN PSQLOOT_DB pDb, IN DWORD dwTableInfoIndex, IN LPCSTR pszColumn)
{
    PSQLOOT_TABLE_INFO  pInfo   = NULL;
    LPCSTR              pszSql  = NULL;
    LPCSTR              psz     = NULL;

    if (!pDb || !pszColumn || dwTableInfoIndex >= pDb->dwTableCount)
        return FALSE;

    // "rowid" is always the rowid
    if (StrCmpIA(pszColumn, "rowid") == 0)
        return TRUE;

    pInfo = &pDb->pTables[dwTableInfoIndex];
    pszSql = pInfo->pszSql;

    if (!pszSql)
    {
        // No schema SQL — fall back to checking if column name is "id"
        return (StrCmpIA(pszColumn, "id") == 0);
    }

    // Search the CREATE TABLE SQL for: column_name INTEGER PRIMARY KEY
    // Walk past "CREATE TABLE name ("
    psz = StrChrA(pszSql, '(');
    if (!psz)
        return (StrCmpIA(pszColumn, "id") == 0);

    psz++;

    // Scan column definitions looking for our column name followed by INTEGER PRIMARY KEY
    while (*psz)
    {
        LPCSTR  pszColStart = NULL;
        LPCSTR  pszColEnd   = NULL;
        INT     nDepth      = 0;

        // Skip whitespace
        while (*psz == ' ' || *psz == '\t' || *psz == '\n' || *psz == '\r')
            psz++;

        if (*psz == ')' || *psz == '\0')
            break;

        // Get column name start
        pszColStart = psz;

        // Handle quoted column names
        if (*psz == '"' || *psz == '\'' || *psz == '`')
        {
            CHAR cQuote = *psz;
            psz++;
            pszColStart = psz;
            while (*psz && *psz != cQuote)
                psz++;
            pszColEnd = psz;
            if (*psz == cQuote)
                psz++;
        }
        else if (*psz == '[')
        {
            psz++;
            pszColStart = psz;
            while (*psz && *psz != ']')
                psz++;
            pszColEnd = psz;
            if (*psz == ']')
                psz++;
        }
        else
        {
            while (*psz && *psz != ' ' && *psz != '\t' && *psz != ',' && *psz != ')' && *psz != '(')
                psz++;
            pszColEnd = psz;
        }

        // Check if this column name matches
        {
            DWORD dwColNameLen = (DWORD)(pszColEnd - pszColStart);
            DWORD dwTargetLen = (DWORD)lstrlenA(pszColumn);

            if (dwColNameLen == dwTargetLen && StrCmpNIA(pszColStart, pszColumn, (INT)dwColNameLen) == 0)
            {
                // Check remainder of this column definition for "INTEGER PRIMARY KEY"
                LPCSTR pszRest = psz;

                while (*pszRest && *pszRest != ',' && *pszRest != ')')
                {
                    if (StrCmpNIA(pszRest, "INTEGER", 7) == 0 &&
                        (pszRest[7] == ' ' || pszRest[7] == '\t'))
                    {
                        LPCSTR pszAfterInt = pszRest + 7;
                        while (*pszAfterInt == ' ' || *pszAfterInt == '\t')
                            pszAfterInt++;

                        if (StrCmpNIA(pszAfterInt, "PRIMARY", 7) == 0 &&
                            (pszAfterInt[7] == ' ' || pszAfterInt[7] == '\t'))
                        {
                            LPCSTR pszAfterPri = pszAfterInt + 7;
                            while (*pszAfterPri == ' ' || *pszAfterPri == '\t')
                                pszAfterPri++;

                            if (StrCmpNIA(pszAfterPri, "KEY", 3) == 0 &&
                                (pszAfterPri[3] == ' ' || pszAfterPri[3] == '\t' ||
                                 pszAfterPri[3] == ',' || pszAfterPri[3] == ')' ||
                                 pszAfterPri[3] == '\0'))
                            {
                                return TRUE;
                            }
                        }
                    }
                    pszRest++;
                }
            }
        }

        // Skip rest of this column definition to next comma
        nDepth = 0;
        while (*psz)
        {
            if (*psz == '(') nDepth++;
            else if (*psz == ')') { if (nDepth == 0) break; nDepth--; }
            else if (*psz == ',' && nDepth == 0) { psz++; break; }
            psz++;
        }
    }

    return FALSE;
}

static VOID AnalyzeJoinForSeek(IN PSQLOOT_STMT pStmt, IN DWORD dwJoinIndex)
{
    PSQLOOT_JOIN    pJoin           = NULL;
    PSQLOOT_EXPR    pCond           = NULL;
    PSQLOOT_EXPR    pLeftExpr       = NULL;
    PSQLOOT_EXPR    pRightExpr      = NULL;
    INT             nRightTableIdx  = 0;
    PSQLOOT_EXPR    pRowIdSide      = NULL;
    PSQLOOT_EXPR    pKeySide        = NULL;
    INT             nRowIdTableIdx  = -1;
    INT             nKeyTableIdx    = -1;

    if (!pStmt || dwJoinIndex >= pStmt->dwJoinCount)
        return;

    pJoin = &pStmt->Joins[dwJoinIndex];
    pCond = pJoin->pOnCondition;

    if (!pCond)
        return;

    // Must be a simple equality comparison: col1 = col2
    if (pCond->nType != SQLOOT_EXPR_COMPARE || pCond->nOperator != SQLOOT_CMP_EQ)
        return;

    pLeftExpr = pCond->pLeft;
    pRightExpr = pCond->pRight;

    if (!pLeftExpr || !pRightExpr)
        return;

    if (pLeftExpr->nType != SQLOOT_EXPR_COLUMN || pRightExpr->nType != SQLOOT_EXPR_COLUMN)
        return;

    nRightTableIdx = pJoin->nRightTableIndex;

    // Determine which side references the right table's rowid
    // Check left side
    if (pLeftExpr->ColRef.szAlias[0])
    {
        INT nIdx = -1;
        if (FindTableByNameOrAlias(pStmt, pLeftExpr->ColRef.szAlias, &nIdx))
        {
            if (nIdx == nRightTableIdx &&
                IsColumnRowId(pStmt->pDb, pStmt->Tables[nIdx].dwTableInfoIndex, pLeftExpr->ColRef.szColumn))
            {
                pRowIdSide = pLeftExpr;
                pKeySide = pRightExpr;
                nRowIdTableIdx = nIdx;
            }
        }
    }

    // Check right side (if left wasn't the rowid)
    if (!pRowIdSide && pRightExpr->ColRef.szAlias[0])
    {
        INT nIdx = -1;
        if (FindTableByNameOrAlias(pStmt, pRightExpr->ColRef.szAlias, &nIdx))
        {
            if (nIdx == nRightTableIdx &&
                IsColumnRowId(pStmt->pDb, pStmt->Tables[nIdx].dwTableInfoIndex, pRightExpr->ColRef.szColumn))
            {
                pRowIdSide = pRightExpr;
                pKeySide = pLeftExpr;
                nRowIdTableIdx = nIdx;
            }
        }
    }

    if (!pRowIdSide || !pKeySide)
        return;

    // Verify the key side references a different table
    if (pKeySide->ColRef.szAlias[0])
    {
        if (!FindTableByNameOrAlias(pStmt, pKeySide->ColRef.szAlias, &nKeyTableIdx))
            return;
        if (nKeyTableIdx == nRightTableIdx)
            return;
    }

    // Enable seek optimization
    pJoin->bUseRowIdSeek = TRUE;
    pJoin->bSeekDone = FALSE;
    CopyMemory(&pJoin->SeekKeyColRef, &pKeySide->ColRef, sizeof(SQLOOT_COLUMN_REF));

}

static BOOL ParseSQL(IN PSQLOOT_DB pDb, IN LPCSTR pszSql, OUT PSQLOOT_STMT pStmt)
{
    CHAR        szToken[SQLOOT_MAX_TABLE_NAME]  = { 0 };
    CHAR        szColumns[SQLOOT_MAX_SQL_LENGTH] = { 0 };
    LPCSTR      pszCurrent      = pszSql;
    LPCSTR      pszSelectStart  = NULL;
    LPCSTR      pszFromStart    = NULL;
    DWORD       dwColCount      = 0;
    DWORD       i               = 0;

    if (!pDb || !pszSql || !pStmt)
        return FALSE;

    ZeroMemory(pStmt, sizeof(SQLOOT_STMT));
    pStmt->pDb = pDb;

    // Find SELECT
    if (!ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)) || StrCmpIA(szToken, "SELECT") != 0)
    {
        SQLOOT_SET_ERROR("SQL Must Start With 'SELECT' [Line:%d]", __LINE__);
        return FALSE;
    }

    pszSelectStart = pszCurrent;

    // Find FROM position
    pszFromStart = StrStrIA(pszCurrent, " FROM ");
    if (!pszFromStart)
    {
        SQLOOT_SET_ERROR("SQL Must Contain 'FROM' Clause [Line:%d]", __LINE__);
        return FALSE;
    }

    // Extract column list
    {
        SIZE_T cchCols = pszFromStart - pszSelectStart;
        if (cchCols >= ARRAYSIZE(szColumns))
            cchCols = ARRAYSIZE(szColumns) - 1;
        StringCchCopyNA(szColumns, ARRAYSIZE(szColumns), pszSelectStart, cchCols);
        TrimWhitespace(szColumns);
    }

    // Parse columns
    {
        LPSTR pszColCopy = DuplicateStringA(szColumns);
        LPSTR pszColToken = NULL;
        LPSTR pszColContext = NULL;

        if (pszColCopy)
        {
            pszColToken = strtok_s(pszColCopy, ",", &pszColContext);
            while (pszColToken)
            {
                dwColCount++;
                pszColToken = strtok_s(NULL, ",", &pszColContext);
            }
            HeapFree(GetProcessHeap(), 0, pszColCopy);
        }

        if (dwColCount > 0)
        {
            pStmt->pSelectColumns = (PSQLOOT_SELECT_COL)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwColCount * sizeof(SQLOOT_SELECT_COL));
            if (!pStmt->pSelectColumns)
                return FALSE;

            pszColCopy = DuplicateStringA(szColumns);
            if (pszColCopy)
            {
                pszColToken = strtok_s(pszColCopy, ",", &pszColContext);
                i = 0;
                while (pszColToken && i < dwColCount)
                {
                    pszColToken = TrimWhitespace(pszColToken);

                    ParseColumnRef(pszColToken, &pStmt->pSelectColumns[i].ColRef);
                    StringCchCopyA(pStmt->pSelectColumns[i].szOutputName, ARRAYSIZE(pStmt->pSelectColumns[i].szOutputName), pStmt->pSelectColumns[i].ColRef.szColumn);
                    i++;
                    pszColToken = strtok_s(NULL, ",", &pszColContext);
                }
                pStmt->dwSelectColumnCount = i;
                HeapFree(GetProcessHeap(), 0, pszColCopy);
            }
        }
    }

    // Move past FROM
    pszCurrent = pszFromStart + 6; // " FROM "

    // Parse first table (FROM table)
    if (!ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)))
    {
        SQLOOT_SET_ERROR("Expected Table Name After 'FROM' [Line:%d]", __LINE__);
        return FALSE;
    }

    StringCchCopyA(pStmt->Tables[0].szTableName, ARRAYSIZE(pStmt->Tables[0].szTableName), szToken);

    // Check for alias
    {
        LPCSTR pszSaved = pszCurrent;
        if (ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)))
        {
            if (StrCmpIA(szToken, "JOIN") != 0 && StrCmpIA(szToken, "WHERE") != 0 &&
                StrCmpIA(szToken, "INNER") != 0 && StrCmpIA(szToken, "ORDER") != 0 &&
                StrCmpIA(szToken, ";") != 0)
            {
                StringCchCopyA(pStmt->Tables[0].szAlias, ARRAYSIZE(pStmt->Tables[0].szAlias), szToken);
            }
            else
            {
                pszCurrent = pszSaved;
            }
        }
    }

    // Find table in schema
    for (i = 0; i < pDb->dwTableCount; i++)
    {
        if (StrCmpIA(pDb->pTables[i].szName, pStmt->Tables[0].szTableName) == 0 &&
            StrCmpIA(pDb->pTables[i].szType, "table") == 0)
        {
            pStmt->Tables[0].dwRootPage = pDb->pTables[i].dwRootPage;
            pStmt->Tables[0].dwTableInfoIndex = i;
            break;
        }
    }

    if (pStmt->Tables[0].dwRootPage == 0)
    {
        SQLOOT_SET_ERROR("Table '%s' Was Not Found [Line:%d]", pStmt->Tables[0].szTableName, __LINE__);
        return FALSE;
    }

    pStmt->dwTableCount = 1;

    // Parse JOINs (INNER JOIN only)
    while (TRUE)
    {
        LPCSTR pszSaved = pszCurrent;

        if (!ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)))
            break;

        // Skip optional INNER keyword
        if (StrCmpIA(szToken, "INNER") == 0)
        {
            if (!ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)))
                break;
        }

        if (StrCmpIA(szToken, "JOIN") != 0)
        {
            pszCurrent = pszSaved;
            break;
        }

        if (pStmt->dwTableCount >= SQLOOT_MAX_TABLES)
        {
            SQLOOT_SET_ERROR("Too Many JOINed Tables (Current:%d, Max:%d) [Line:%d]", pStmt->dwTableCount, SQLOOT_MAX_TABLES, __LINE__);
            return FALSE;
        }

        // Parse joined table name
        if (!ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)))
        {
            SQLOOT_SET_ERROR("Expected Table Name After 'JOIN' [Line:%d]", __LINE__);
            return FALSE;
        }

        StringCchCopyA(pStmt->Tables[pStmt->dwTableCount].szTableName, ARRAYSIZE(pStmt->Tables[pStmt->dwTableCount].szTableName), szToken);

        // Check for alias
        pszSaved = pszCurrent;
        if (ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)))
        {
            if (StrCmpIA(szToken, "ON") != 0)
                StringCchCopyA(pStmt->Tables[pStmt->dwTableCount].szAlias, ARRAYSIZE(pStmt->Tables[pStmt->dwTableCount].szAlias), szToken);
            else
                pszCurrent = pszSaved;
        }

        // Find table in schema
        for (i = 0; i < pDb->dwTableCount; i++)
        {
            if (StrCmpIA(pDb->pTables[i].szName, pStmt->Tables[pStmt->dwTableCount].szTableName) == 0 &&
                StrCmpIA(pDb->pTables[i].szType, "table") == 0)
            {
                pStmt->Tables[pStmt->dwTableCount].dwRootPage = pDb->pTables[i].dwRootPage;
                pStmt->Tables[pStmt->dwTableCount].dwTableInfoIndex = i;
                break;
            }
        }

        if (pStmt->Tables[pStmt->dwTableCount].dwRootPage == 0)
        {
            SQLOOT_SET_ERROR("Table '%s' Was Not Found [Line:%d]", pStmt->Tables[pStmt->dwTableCount].szTableName, __LINE__);
            return FALSE;
        }

        // Parse ON clause
        pStmt->Joins[pStmt->dwJoinCount].nRightTableIndex = (INT)pStmt->dwTableCount;
        pStmt->Joins[pStmt->dwJoinCount].nJoinType = SQLOOT_JOIN_INNER;
        pStmt->Joins[pStmt->dwJoinCount].bHadMatch = FALSE;

        {
            LPCSTR pszSaved2 = pszCurrent;
            if (ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)) && StrCmpIA(szToken, "ON") == 0)
            {
                pStmt->Joins[pStmt->dwJoinCount].pOnCondition = ParseExpression(&pszCurrent);
                if (!pStmt->Joins[pStmt->dwJoinCount].pOnCondition)
                {
                    SQLOOT_SET_ERROR("Failed To Parse 'ON' Condition [Line:%d]", __LINE__);
                    return FALSE;
                }
            }
            else
            {
                SQLOOT_SET_ERROR("Expected 'ON' Clause After 'JOIN' [Line:%d]", __LINE__);
                return FALSE;
            }
        }

        pStmt->dwJoinCount++;
        pStmt->dwTableCount++;
    }

    // Parse WHERE clause
    {
        LPCSTR pszSaved = pszCurrent;
        if (ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)) && StrCmpIA(szToken, "WHERE") == 0)
        {
            pStmt->pWhereExpr = ParseExpression(&pszCurrent);
            if (!pStmt->pWhereExpr)
            {
                SQLOOT_SET_ERROR("Failed To Parse 'WHERE' Condition [Line:%d]", __LINE__);
                return FALSE;
            }
        }
        else
        {
            pszCurrent = pszSaved;
        }
    }

    // Parse ORDER BY clause
    {
        LPCSTR pszSaved = pszCurrent;
        if (ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)) && StrCmpIA(szToken, "ORDER") == 0)
        {
            if (ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)) && StrCmpIA(szToken, "BY") == 0)
            {
                // Count ORDER BY items
                DWORD dwOrderCount = 1;
                LPCSTR pszOrderStart = pszCurrent;
                LPCSTR pszTemp = pszCurrent;
                while (*pszTemp)
                {
                    if (*pszTemp == ',')
                        dwOrderCount++;
                    else if (*pszTemp == ';')
                        break;
                    pszTemp++;
                }

                pStmt->pOrderBy = (PSQLOOT_ORDER_BY)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwOrderCount * sizeof(SQLOOT_ORDER_BY));
                if (!pStmt->pOrderBy)
                {
                    SQLOOT_SET_ERROR("HeapAlloc Failed With Error: %lu [Line:%d]", GetLastError(), __LINE__);
                    return FALSE;
                }

                // Parse ORDER BY items
                pszCurrent = pszOrderStart;
                DWORD dwIdx = 0;

                while (dwIdx < dwOrderCount)
                {
                    PSQLOOT_EXPR pOrderExpr = ParseAndExpression(&pszCurrent);
                    if (pOrderExpr)
                    {
                        CopyMemory(&pStmt->pOrderBy[dwIdx].Expr, pOrderExpr, sizeof(SQLOOT_EXPR));
                        HeapFree(GetProcessHeap(), 0, pOrderExpr);

                        pStmt->pOrderBy[dwIdx].nSortOrder = SQLOOT_SORT_ASC; // Default

                        // Check for ASC/DESC
                        pszSaved = pszCurrent;
                        if (ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)))
                        {
                            if (StrCmpIA(szToken, "DESC") == 0)
                            {
                                pStmt->pOrderBy[dwIdx].nSortOrder = SQLOOT_SORT_DESC;
                            }
                            else if (StrCmpIA(szToken, "ASC") == 0)
                            {
                                pStmt->pOrderBy[dwIdx].nSortOrder = SQLOOT_SORT_ASC;
                            }
                            else if (StrCmpIA(szToken, ",") == 0)
                            {
                                dwIdx++;
                                continue;
                            }
                            else
                            {
                                pszCurrent = pszSaved;
                            }
                        }

                        dwIdx++;

                        // Check for comma
                        pszSaved = pszCurrent;
                        if (ParseToken(&pszCurrent, szToken, ARRAYSIZE(szToken)))
                        {
                            if (StrCmpIA(szToken, ",") != 0)
                            {
                                pszCurrent = pszSaved;
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }

                pStmt->dwOrderByCount = dwIdx;
            }
            else
            {
                pszCurrent = pszSaved;
            }
        }
        else
        {
            pszCurrent = pszSaved;
        }
    }

    return TRUE;
}

static VOID ParseCreateTableColumns(IN OUT PSQLOOT_TABLE_INFO pTableInfo)
{
    LPCSTR  psz         = NULL;
    LPCSTR  pszStart    = NULL;
    LPCSTR  pszEnd      = NULL;
    INT     nDepth      = 0;
    DWORD   dwCol       = 0;

    if (!pTableInfo || !pTableInfo->pszSql)
        return;

    // Find first '(' after CREATE TABLE name
    psz = StrChrA(pTableInfo->pszSql, '(');
    if (!psz)
        return;

    psz++; // skip '('

    while (*psz && dwCol < SQLOOT_MAX_COLUMNS)
    {
        // Skip whitespace
        while (*psz == ' ' || *psz == '\t' || *psz == '\n' || *psz == '\r')
            psz++;

        if (*psz == ')' || *psz == '\0')
            break;

        // Skip constraints: PRIMARY, UNIQUE, CHECK, FOREIGN, CONSTRAINT
        if (StrCmpNIA(psz, "PRIMARY",    7) == 0 && (psz[7]  == ' ' || psz[7]  == '(') ||
            StrCmpNIA(psz, "UNIQUE",     6) == 0 && (psz[6]  == ' ' || psz[6]  == '(') ||
            StrCmpNIA(psz, "CHECK",      5) == 0 && (psz[5]  == ' ' || psz[5]  == '(') ||
            StrCmpNIA(psz, "FOREIGN",    7) == 0 && (psz[7]  == ' ') ||
            StrCmpNIA(psz, "CONSTRAINT", 10) == 0 && (psz[10] == ' '))
        {
            // Skip to next comma at depth 0 or closing ')'
            nDepth = 0;
            while (*psz)
            {
                if (*psz == '(') nDepth++;
                else if (*psz == ')') { if (nDepth == 0) break; nDepth--; }
                else if (*psz == ',' && nDepth == 0) { psz++; break; }
                psz++;
            }
            continue;
        }

        // Extract column name, handling quoted identifiers
        pszStart = psz;

        if (*psz == '"' || *psz == '\'' || *psz == '`')
        {
            CHAR cQuote = *psz;
            psz++;      // skip opening quote
            pszStart = psz;
            while (*psz && *psz != cQuote)
                psz++;
            pszEnd = psz;
            if (*psz == cQuote)
                psz++;  // skip closing quote
        }
        else if (*psz == '[')
        {
            psz++;      // skip '['
            pszStart = psz;
            while (*psz && *psz != ']')
                psz++;
            pszEnd = psz;
            if (*psz == ']')
                psz++;  // skip ']'
        }
        else
        {
            while (*psz && *psz != ' ' && *psz != '\t' && *psz != ',' && *psz != ')' && *psz != '(')
                psz++;
            pszEnd = psz;
        }

        if (pszEnd > pszStart && (DWORD)(pszEnd - pszStart) < SQLOOT_MAX_TABLE_NAME)
        {
            CopyMemory(pTableInfo->szColumnNames[dwCol], pszStart, (DWORD)(pszEnd - pszStart));
            pTableInfo->szColumnNames[dwCol][pszEnd - pszStart] = '\0';
            dwCol++;
        }

        // Skip the rest of this column definition (type, constraints) to next comma
        nDepth = 0;
        while (*psz)
        {
            if (*psz == '(') nDepth++;
            else if (*psz == ')') { if (nDepth == 0) break; nDepth--; }
            else if (*psz == ',' && nDepth == 0) { psz++; break; }
            psz++;
        }
    }

    pTableInfo->dwSchemaColumnCount = dwCol;
}

#pragma endregion

// ===============================================================================================================================================================
// EXPRESSION EVALUATION
// ===============================================================================================================================================================

#pragma region EXPRESSION_EVALUATION

static PSQLOOT_VALUE GetColumnValue(IN PSQLOOT_STMT pStmt, IN PSQLOOT_COLUMN_REF pColRef)
{
    INT     nTableIndex = -1;
    DWORD   i           = 0;

    if (!pStmt || !pColRef)
        return NULL;

    // Find the table
    if (pColRef->szAlias[0])
    {
        if (!FindTableByNameOrAlias(pStmt, pColRef->szAlias, &nTableIndex))
            return NULL;
    }
    else if (pStmt->dwTableCount == 1)
    {
        nTableIndex = 0;
    }
    else
    {
        // try each table
        for (i = 0; i < pStmt->dwTableCount; i++)
        {
            if (pStmt->Tables[i].bHasRow)
            {
                nTableIndex = (INT)i;
                break;
            }
        }
    }

    if (nTableIndex < 0 || nTableIndex >= (INT)pStmt->dwTableCount)
        return NULL;

    if (!pStmt->Tables[nTableIndex].bHasRow)
        return NULL;

    {
        PSQLOOT_ROW pRow = &pStmt->Tables[nTableIndex].CurrentRow;
        INT nColIndex = -1;

        // Try numeric index first (for internal use)
        if (pColRef->nColumnIndex >= 0 && (DWORD)pColRef->nColumnIndex < pRow->dwColumnCount)
        {
            return &pRow->pColumns[pColRef->nColumnIndex].Value;
        }

        // Special column names: id, rowid
        if (StrCmpIA(pColRef->szColumn, "id") == 0 || StrCmpIA(pColRef->szColumn, "rowid") == 0)
        {
            pStmt->Tables[nTableIndex].RowIdValue.nType = SQLOOT_TYPE_INT64;
            pStmt->Tables[nTableIndex].RowIdValue.i64Value = pRow->i64RowId;
            return &pStmt->Tables[nTableIndex].RowIdValue;
        }

        if (pRow->dwColumnCount > 0)
        {
            LPSTR pszEnd = NULL;
            nColIndex = (INT)strtol(pColRef->szColumn, &pszEnd, 10);
            if (pszEnd != pColRef->szColumn && *pszEnd == '\0')
            {
                if (nColIndex >= 0 && (DWORD)nColIndex < pRow->dwColumnCount)
                    return &pRow->pColumns[nColIndex].Value;
            }

            // Look up column by name from parsed CREATE TABLE schema
            {
                PSQLOOT_TABLE_INFO pInfo = &pStmt->pDb->pTables[pStmt->Tables[nTableIndex].dwTableInfoIndex];
                DWORD k = 0;

                for (k = 0; k < pInfo->dwSchemaColumnCount; k++)
                {
                    if (StrCmpIA(pInfo->szColumnNames[k], pColRef->szColumn) == 0)
                    {
                        if (k < pRow->dwColumnCount)
                            return &pRow->pColumns[k].Value;
                        break;
                    }
                }
            }
        }
    }

    return NULL;
}

static BOOL EvaluateExprToValue(IN PSQLOOT_STMT pStmt, IN PSQLOOT_EXPR pExpr, OUT PSQLOOT_VALUE pResult)
{
    if (!pStmt || !pExpr || !pResult)
        return FALSE;

    ZeroMemory(pResult, sizeof(SQLOOT_VALUE));

    switch (pExpr->nType)
    {
        case SQLOOT_EXPR_NULL:
            pResult->nType = SQLOOT_TYPE_NULL;
            return TRUE;

        case SQLOOT_EXPR_INTEGER:
            pResult->nType = SQLOOT_TYPE_INT64;
            pResult->i64Value = pExpr->i64Value;
            return TRUE;

        case SQLOOT_EXPR_FLOAT:
            pResult->nType = SQLOOT_TYPE_FLOAT64;
            pResult->dValue = pExpr->dValue;
            return TRUE;

        case SQLOOT_EXPR_STRING:
            pResult->nType = SQLOOT_TYPE_TEXT;
            pResult->Text.pszData = pExpr->szStrValue;
            pResult->Text.dwLength = (DWORD)lstrlenA(pExpr->szStrValue);
            return TRUE;

        case SQLOOT_EXPR_COLUMN:
        {
            PSQLOOT_VALUE pVal = GetColumnValue(pStmt, &pExpr->ColRef);
            if (pVal)
            {
                CopyMemory(pResult, pVal, sizeof(SQLOOT_VALUE));
                return TRUE;
            }
            pResult->nType = SQLOOT_TYPE_NULL;
            return TRUE;
        }

        case SQLOOT_EXPR_FUNCTION:
        {
            SQLOOT_VALUE ArgValue = { 0 };

            if (!pExpr->pFuncArg || !EvaluateExprToValue(pStmt, pExpr->pFuncArg, &ArgValue))
            {
                pResult->nType = SQLOOT_TYPE_NULL;
                return TRUE;
            }

            if (pExpr->nFuncType == SQLOOT_FUNC_LENGTH)
            {
                pResult->nType = SQLOOT_TYPE_INT64;
                if (ArgValue.nType == SQLOOT_TYPE_TEXT && ArgValue.Text.pszData)
                    pResult->i64Value = (INT64)lstrlenA(ArgValue.Text.pszData);
                else if (ArgValue.nType == SQLOOT_TYPE_BLOB)
                    pResult->i64Value = (INT64)ArgValue.Blob.dwSize;
                else if (ArgValue.nType == SQLOOT_TYPE_NULL)
                    pResult->nType = SQLOOT_TYPE_NULL;
                else
                    pResult->i64Value = 0;
                return TRUE;
            }
            break;
        }
    }

    pResult->nType = SQLOOT_TYPE_NULL;
    return TRUE;
}

static INT CompareValues(IN PSQLOOT_VALUE pLeft, IN PSQLOOT_VALUE pRight)
{
    if (!pLeft || !pRight)
        return 0;

    // NULL handling
    if (pLeft->nType == SQLOOT_TYPE_NULL && pRight->nType == SQLOOT_TYPE_NULL)
        return 0;
    if (pLeft->nType == SQLOOT_TYPE_NULL)
        return -1;
    if (pRight->nType == SQLOOT_TYPE_NULL)
        return 1;

    // Integer comparison
    if ((pLeft->nType >= SQLOOT_TYPE_INT8 && pLeft->nType <= SQLOOT_TYPE_ONE) &&
        (pRight->nType >= SQLOOT_TYPE_INT8 && pRight->nType <= SQLOOT_TYPE_ONE))
    {
        if (pLeft->i64Value < pRight->i64Value) return -1;
        if (pLeft->i64Value > pRight->i64Value) return 1;
        return 0;
    }

    // Float comparison
    if (pLeft->nType == SQLOOT_TYPE_FLOAT64 || pRight->nType == SQLOOT_TYPE_FLOAT64)
    {
        DOUBLE dLeft = (pLeft->nType == SQLOOT_TYPE_FLOAT64) ? pLeft->dValue : (DOUBLE)pLeft->i64Value;
        DOUBLE dRight = (pRight->nType == SQLOOT_TYPE_FLOAT64) ? pRight->dValue : (DOUBLE)pRight->i64Value;

        if (dLeft != dLeft && dRight != dRight) return 0;
        if (dLeft != dLeft)  return -1;
        if (dRight != dRight) return 1;
        if (dLeft < dRight) return -1;
        if (dLeft > dRight) return 1;
        return 0;
    }

    // Text comparison
    if (pLeft->nType == SQLOOT_TYPE_TEXT && pRight->nType == SQLOOT_TYPE_TEXT)
    {
        if (pLeft->Text.pszData && pRight->Text.pszData)
            return StrCmpIA(pLeft->Text.pszData, pRight->Text.pszData);
    }

    // Mixed: integer vs text
    if ((pLeft->nType >= SQLOOT_TYPE_INT8 && pLeft->nType <= SQLOOT_TYPE_ONE) &&
        pRight->nType == SQLOOT_TYPE_TEXT)
    {
        INT64 i64Right = pRight->Text.pszData ? _atoi64(pRight->Text.pszData) : 0;
        if (pLeft->i64Value < i64Right) return -1;
        if (pLeft->i64Value > i64Right) return 1;
        return 0;
    }

    if (pLeft->nType == SQLOOT_TYPE_TEXT &&
        (pRight->nType >= SQLOOT_TYPE_INT8 && pRight->nType <= SQLOOT_TYPE_ONE))
    {
        INT64 i64Left = pLeft->Text.pszData ? _atoi64(pLeft->Text.pszData) : 0;
        if (i64Left < pRight->i64Value) return -1;
        if (i64Left > pRight->i64Value) return 1;
        return 0;
    }

    return 0;
}

static BOOL EvaluateCondition(IN PSQLOOT_STMT pStmt, IN PSQLOOT_EXPR pExpr)
{
    if (!pStmt || !pExpr)
        return FALSE;

    switch (pExpr->nType)
    {
        case SQLOOT_EXPR_AND:
            return EvaluateCondition(pStmt, pExpr->pLeft) && EvaluateCondition(pStmt, pExpr->pRight);

        case SQLOOT_EXPR_IS_NULL:
        {
            if (pExpr->pLeft && pExpr->pLeft->nType == SQLOOT_EXPR_COLUMN)
            {
                PSQLOOT_VALUE pVal = GetColumnValue(pStmt, &pExpr->pLeft->ColRef);
                return (!pVal || pVal->nType == SQLOOT_TYPE_NULL);
            }
            return FALSE;
        }

        case SQLOOT_EXPR_IS_NOT_NULL:
        {
            if (pExpr->pLeft && pExpr->pLeft->nType == SQLOOT_EXPR_COLUMN)
            {
                PSQLOOT_VALUE pVal = GetColumnValue(pStmt, &pExpr->pLeft->ColRef);
                return (pVal && pVal->nType != SQLOOT_TYPE_NULL);
            }
            return FALSE;
        }

        case SQLOOT_EXPR_COMPARE:
        {
            SQLOOT_VALUE    LeftVal = { 0 };
            SQLOOT_VALUE    RightVal = { 0 };
            INT             nCmp = 0;

            if (!pExpr->pLeft || !pExpr->pRight)
                return FALSE;

            if (!EvaluateExprToValue(pStmt, pExpr->pLeft, &LeftVal))
                return FALSE;

            if (!EvaluateExprToValue(pStmt, pExpr->pRight, &RightVal))
                return FALSE;

            nCmp = CompareValues(&LeftVal, &RightVal);

            switch (pExpr->nOperator)
            {
            case SQLOOT_CMP_EQ:     return (nCmp == 0);
            case SQLOOT_CMP_NE:     return (nCmp != 0);
            case SQLOOT_CMP_LT:     return (nCmp < 0);
            case SQLOOT_CMP_LE:     return (nCmp <= 0);
            case SQLOOT_CMP_GT:     return (nCmp > 0);
            case SQLOOT_CMP_GE:     return (nCmp >= 0);
            }
        }

        break;
    }

    return FALSE;
}

static BOOL CopyValue(IN PSQLOOT_VALUE pSrc, OUT PSQLOOT_VALUE pDst)
{
    if (!pSrc || !pDst)
        return FALSE;

    pDst->nType = pSrc->nType;

    switch (pSrc->nType)
    {
        case SQLOOT_TYPE_NULL:
            break;
        case SQLOOT_TYPE_INT8:
        case SQLOOT_TYPE_INT16:
        case SQLOOT_TYPE_INT24:
        case SQLOOT_TYPE_INT32:
        case SQLOOT_TYPE_INT48:
        case SQLOOT_TYPE_INT64:
        case SQLOOT_TYPE_ZERO:
        case SQLOOT_TYPE_ONE:
            pDst->i64Value = pSrc->i64Value;
            break;
        case SQLOOT_TYPE_FLOAT64:
            pDst->dValue = pSrc->dValue;
            break;
        case SQLOOT_TYPE_TEXT:
            pDst->Text.dwLength = pSrc->Text.dwLength;
            if (pSrc->Text.pszData)
                pDst->Text.pszData = DuplicateStringA(pSrc->Text.pszData);
            break;
        case SQLOOT_TYPE_BLOB:
            pDst->Blob.dwSize = pSrc->Blob.dwSize;
            if (pSrc->Blob.pbData && pSrc->Blob.dwSize > 0)
            {
                pDst->Blob.pbData = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pSrc->Blob.dwSize);
                if (pDst->Blob.pbData)
                    CopyMemory(pDst->Blob.pbData, pSrc->Blob.pbData, pSrc->Blob.dwSize);
            }
            break;
    }

    return TRUE;
}

static BOOL DuplicateRow(IN PSQLOOT_ROW pSrc, OUT PSQLOOT_ROW pDst)
{
    DWORD i = 0;

    if (!pSrc || !pDst)
        return FALSE;

    ZeroMemory(pDst, sizeof(SQLOOT_ROW));

    pDst->i64RowId = pSrc->i64RowId;
    pDst->dwColumnCount = pSrc->dwColumnCount;

    if (pSrc->dwColumnCount > 0 && pSrc->pColumns)
    {
        pDst->pColumns = (PSQLOOT_COLUMN)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pSrc->dwColumnCount * sizeof(SQLOOT_COLUMN));
        if (!pDst->pColumns)
            return FALSE;

        for (i = 0; i < pSrc->dwColumnCount; i++)
        {
            StringCchCopyA(pDst->pColumns[i].szName, ARRAYSIZE(pDst->pColumns[i].szName),
                          pSrc->pColumns[i].szName);
            CopyValue(&pSrc->pColumns[i].Value, &pDst->pColumns[i].Value);
        }
    }

    return TRUE;
}

static INT CompareRowsForSort(const void* pA, const void* pB)
{
    PSQLOOT_ROW pRowA = (PSQLOOT_ROW)pA;
    PSQLOOT_ROW pRowB = (PSQLOOT_ROW)pB;
    DWORD i = 0;
    INT nResult = 0;

    if (!g_pSortStmt || !g_pSortStmt->pOrderBy)
        return 0;

    for (i = 0; i < g_pSortStmt->dwOrderByCount; i++)
    {
        PSQLOOT_ORDER_BY pOrder = &g_pSortStmt->pOrderBy[i];
        SQLOOT_VALUE ValA = { 0 };
        SQLOOT_VALUE ValB = { 0 };

        if (pOrder->Expr.nType == SQLOOT_EXPR_COLUMN)
        {
            INT nColIdx = -1;
            DWORD j = 0;

            for (j = 0; j < pRowA->dwColumnCount; j++)
            {
                if (StrCmpIA(pRowA->pColumns[j].szName, pOrder->Expr.ColRef.szColumn) == 0)
                {
                    nColIdx = (INT)j;
                    break;
                }
            }

            if (nColIdx >= 0 && (DWORD)nColIdx < pRowA->dwColumnCount)
                CopyMemory(&ValA, &pRowA->pColumns[nColIdx].Value, sizeof(SQLOOT_VALUE));
            if (nColIdx >= 0 && (DWORD)nColIdx < pRowB->dwColumnCount)
                CopyMemory(&ValB, &pRowB->pColumns[nColIdx].Value, sizeof(SQLOOT_VALUE));
        }
        else if (pOrder->Expr.nType == SQLOOT_EXPR_FUNCTION && pOrder->Expr.nFuncType == SQLOOT_FUNC_LENGTH && pOrder->Expr.pFuncArg)
        {
            if (pOrder->Expr.pFuncArg->nType == SQLOOT_EXPR_COLUMN)
            {
                INT nColIdx = -1;
                DWORD j = 0;

                for (j = 0; j < pRowA->dwColumnCount; j++)
                {
                    if (StrCmpIA(pRowA->pColumns[j].szName, pOrder->Expr.pFuncArg->ColRef.szColumn) == 0)
                    {
                        nColIdx = (INT)j;
                        break;
                    }
                }

                ValA.nType = SQLOOT_TYPE_INT64;
                ValB.nType = SQLOOT_TYPE_INT64;

                if (nColIdx >= 0 && (DWORD)nColIdx < pRowA->dwColumnCount)
                {
                    PSQLOOT_VALUE pVal = &pRowA->pColumns[nColIdx].Value;
                    if (pVal->nType == SQLOOT_TYPE_TEXT && pVal->Text.pszData)
                        ValA.i64Value = (INT64)lstrlenA(pVal->Text.pszData);
                    else if (pVal->nType == SQLOOT_TYPE_BLOB)
                        ValA.i64Value = (INT64)pVal->Blob.dwSize;
                    else if (pVal->nType == SQLOOT_TYPE_NULL)
                        ValA.nType = SQLOOT_TYPE_NULL;
                }

                if (nColIdx >= 0 && (DWORD)nColIdx < pRowB->dwColumnCount)
                {
                    PSQLOOT_VALUE pVal = &pRowB->pColumns[nColIdx].Value;
                    if (pVal->nType == SQLOOT_TYPE_TEXT && pVal->Text.pszData)
                        ValB.i64Value = (INT64)lstrlenA(pVal->Text.pszData);
                    else if (pVal->nType == SQLOOT_TYPE_BLOB)
                        ValB.i64Value = (INT64)pVal->Blob.dwSize;
                    else if (pVal->nType == SQLOOT_TYPE_NULL)
                        ValB.nType = SQLOOT_TYPE_NULL;
                }
            }
        }

        nResult = CompareValues(&ValA, &ValB);

        if (nResult != 0)
        {
            if (pOrder->nSortOrder == SQLOOT_SORT_DESC)
                nResult = -nResult;
            return nResult;
        }
    }

    return 0;
}

static BOOL AddRowToBuffer(IN PSQLOOT_STMT pStmt, IN PSQLOOT_ROW pRow)
{
    if (!pStmt || !pRow)
        return FALSE;

    // Grow buffer if needed
    if (pStmt->dwResultBufferCount >= pStmt->dwResultBufferCapacity)
    {
        DWORD dwNewCapacity = pStmt->dwResultBufferCapacity == 0 ? 256 : pStmt->dwResultBufferCapacity * 2;
        PSQLOOT_ROW pNewBuffer = (PSQLOOT_ROW)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwNewCapacity * sizeof(SQLOOT_ROW));
        if (!pNewBuffer)
            return FALSE;

        if (pStmt->pResultBuffer && pStmt->dwResultBufferCount > 0)
        {
            CopyMemory(pNewBuffer, pStmt->pResultBuffer, pStmt->dwResultBufferCount * sizeof(SQLOOT_ROW));
            HeapFree(GetProcessHeap(), 0, pStmt->pResultBuffer);
        }

        pStmt->pResultBuffer = pNewBuffer;
        pStmt->dwResultBufferCapacity = dwNewCapacity;
    }

    if (!DuplicateRow(pRow, &pStmt->pResultBuffer[pStmt->dwResultBufferCount]))
        return FALSE;

    pStmt->dwResultBufferCount++;
    return TRUE;
}

static VOID SortResultBuffer(IN PSQLOOT_STMT pStmt)
{
    if (!pStmt || !pStmt->pResultBuffer || pStmt->dwResultBufferCount <= 1)
        return;

    if (!pStmt->pOrderBy || pStmt->dwOrderByCount == 0)
        return;

    g_pSortStmt = pStmt;
    qsort(pStmt->pResultBuffer, pStmt->dwResultBufferCount, sizeof(SQLOOT_ROW), CompareRowsForSort);
    g_pSortStmt = NULL;
}

static BOOL BuildResultRow(IN PSQLOOT_STMT pStmt)
{
    DWORD i = 0;

    if (!pStmt)
        return FALSE;

    FreeRow(&pStmt->ResultRow);

    pStmt->ResultRow.pColumns = (PSQLOOT_COLUMN)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pStmt->dwSelectColumnCount * sizeof(SQLOOT_COLUMN));
    if (!pStmt->ResultRow.pColumns)
        return FALSE;

    pStmt->ResultRow.dwColumnCount = pStmt->dwSelectColumnCount;

    for (i = 0; i < pStmt->dwSelectColumnCount; i++)
    {
        PSQLOOT_VALUE pVal = GetColumnValue(pStmt, &pStmt->pSelectColumns[i].ColRef);
        if (pVal)
        {
            CopyValue(pVal, &pStmt->ResultRow.pColumns[i].Value);
            StringCchCopyA(pStmt->ResultRow.pColumns[i].szName, ARRAYSIZE(pStmt->ResultRow.pColumns[i].szName), pStmt->pSelectColumns[i].szOutputName);
        }
        else
        {
            pStmt->ResultRow.pColumns[i].Value.nType = SQLOOT_TYPE_NULL;
        }
    }

    pStmt->ResultRow.i64RowId = pStmt->Tables[0].bHasRow ? pStmt->Tables[0].CurrentRow.i64RowId : 0;
    return TRUE;
}

#pragma endregion

// ===============================================================================================================================================================
// MAIN API IMPLEMENTATIONS
// ===============================================================================================================================================================

#pragma region MAIN_APIS_IMPLEMENTATIONS

INT SQLootOpen(IN LPCSTR pszFilePath, OUT PSQLOOT_DB* ppDb, IN INT nFlags)
{
    PSQLOOT_DB      pDb         = NULL;
    PBYTE           pbFileData  = NULL;
    DWORD           dwFileSize  = 0;
    PSQLOOT_HEADER  pHeader     = NULL;
    WORD            wPageSize   = 0;

    if (!pszFilePath || !ppDb)
    {
        SQLOOT_SET_ERROR("Invalid Parameters [Line:%d]", __LINE__);
        return SQLOOT_RESULT_ERROR;
    }

    *ppDb = NULL;

    if (!(nFlags & SQLOOT_OPEN_READONLY))
    {
        // Only SQLOOT_OPEN_READONLY is supported
        SQLOOT_SET_ERROR("Invalid Flags [Line:%d]", __LINE__);
        return SQLOOT_RESULT_ERROR;
    }

    if (!PathFileExistsA(pszFilePath))
    {
        SQLOOT_SET_ERROR("PathFileExistsA Failed With Error: %lu [Line:%d]", GetLastError(), __LINE__);
        return SQLOOT_RESULT_ERROR;
    }

    if (!ReadFileFromDiskA(pszFilePath, &pbFileData, &dwFileSize))
    {
        SQLOOT_SET_ERROR("Failed To Read Database File [Line:%d]", __LINE__);
        return SQLOOT_RESULT_ERROR;
    }

    if (dwFileSize < SQLOOT_HEADER_SIZE)
    {
        SQLOOT_SET_ERROR("Invalid File Size [Line:%d]", __LINE__);
        HeapFree(GetProcessHeap(), 0, pbFileData);
        return SQLOOT_RESULT_NOTADB;
    }

    pHeader = (PSQLOOT_HEADER)pbFileData;
    if (StrCmpNA(pHeader->szMagic, SQLOOT_MAGIC, sizeof(SQLOOT_MAGIC) - 1) != 0)
    {
        SQLOOT_SET_ERROR("Invalid SQLite File Headers [Line:%d]", __LINE__);
        HeapFree(GetProcessHeap(), 0, pbFileData);
        return SQLOOT_RESULT_NOTADB;
    }

    pDb = (PSQLOOT_DB)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_DB));
    if (!pDb)
    {
        SQLOOT_SET_ERROR("HeapAlloc Failed With Error: %lu [Line:%d]", GetLastError(), __LINE__);
        HeapFree(GetProcessHeap(), 0, pbFileData);
        return SQLOOT_RESULT_ERROR;
    }

    wPageSize = ReadBigEndian16(pbFileData + 16);
    if (wPageSize == 1)
        pDb->dwPageSize = 65536;
    else
        pDb->dwPageSize = wPageSize;

    pDb->pbFileData     = pbFileData;
    pDb->dwFileSize     = dwFileSize;
    pDb->dwPageCount    = dwFileSize / pDb->dwPageSize;
    pDb->dwTextEncoding = ReadBigEndian32(pbFileData + 56);
    pDb->dwSchemaFormat = ReadBigEndian32(pbFileData + 44);
    pDb->bIsOpen        = TRUE;

    if (!ParseSchema(pDb))
    {
        SQLootClose(pDb);
        return SQLOOT_RESULT_ERROR;
    }

    *ppDb = pDb;
    return SQLOOT_RESULT_OK;
}

INT SQLootClose(IN PSQLOOT_DB pDb)
{
    DWORD i = 0;

    if (!pDb)
        return SQLOOT_RESULT_ERROR;

    if (pDb->pbFileData)
        HeapFree(GetProcessHeap(), 0, pDb->pbFileData);

    if (pDb->pTables)
    {
        for (i = 0; i < pDb->dwTableCount; i++)
        {
            if (pDb->pTables[i].pszSql)
                HeapFree(GetProcessHeap(), 0, pDb->pTables[i].pszSql);
        }
        HeapFree(GetProcessHeap(), 0, pDb->pTables);
    }

    HeapFree(GetProcessHeap(), 0, pDb);
    return SQLOOT_RESULT_OK;
}

INT SQLootPrepare(IN PSQLOOT_DB pDb, IN LPCSTR pszSql, IN INT nSqlLength, OUT PSQLOOT_STMT* ppStmt)
{
    PSQLOOT_STMT    pStmt   = NULL;
    DWORD           i       = 0;

    UNREFERENCED_PARAMETER(nSqlLength);

    if (!pDb || !pDb->bIsOpen || !pszSql || !ppStmt)
    {
        SQLOOT_SET_ERROR("Invalid Parameters [Line:%d]", __LINE__);
        return SQLOOT_RESULT_ERROR;
    }

    *ppStmt = NULL;

    pStmt = (PSQLOOT_STMT)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SQLOOT_STMT));
    if (!pStmt)
    {
        SQLOOT_SET_ERROR("HeapAlloc Failed With Error: %lu [Line:%d]", GetLastError(), __LINE__);
        return SQLOOT_RESULT_ERROR;
    }

    if (!ParseSQL(pDb, pszSql, pStmt))
    {
        if (pStmt->pSelectColumns)
            HeapFree(GetProcessHeap(), 0, pStmt->pSelectColumns);
        if (pStmt->pWhereExpr)
            FreeExpression(pStmt->pWhereExpr);
        for (i = 0; i < pStmt->dwJoinCount; i++)
        {
            if (pStmt->Joins[i].pOnCondition)
                FreeExpression(pStmt->Joins[i].pOnCondition);
        }
        HeapFree(GetProcessHeap(), 0, pStmt);
        return SQLOOT_RESULT_ERROR;
    }

    // Initialize table iterators
    for (i = 0; i < pStmt->dwTableCount; i++)
    {
        if (!InitTableIterator(pDb, &pStmt->Tables[i]))
        {
            SQLOOT_SET_ERROR("Failed To Initialize Table Iterator [Line:%d]", __LINE__);
            SQLootFinalize(pStmt);
            return SQLOOT_RESULT_ERROR;
        }
    }

    pStmt->bInitialized = FALSE;

    // Analyze JOIN conditions for rowid seek optimization
    for (i = 0; i < pStmt->dwJoinCount; i++)
        AnalyzeJoinForSeek(pStmt, i);

    *ppStmt = pStmt;
    return SQLOOT_RESULT_OK;
}

INT SQLootStep(IN PSQLOOT_STMT pStmt)
{
    DWORD   i           = 0;

    if (!pStmt || !pStmt->pDb)
        return SQLOOT_RESULT_ERROR;

    // If ORDER BY is present and results are already buffered, return from buffer
    if (pStmt->bResultsBuffered)
    {
        FreeRow(&pStmt->ResultRow);
        pStmt->bHasRow = FALSE;

        if (pStmt->dwCurrentResultIndex < pStmt->dwResultBufferCount)
        {
            if (DuplicateRow(&pStmt->pResultBuffer[pStmt->dwCurrentResultIndex], &pStmt->ResultRow))
            {
                pStmt->dwCurrentResultIndex++;
                pStmt->dwRowsReturned++;
                pStmt->bHasRow = TRUE;
                pStmt->nLastResult = SQLOOT_RESULT_ROW;
                return SQLOOT_RESULT_ROW;
            }
        }

        pStmt->nLastResult = SQLOOT_RESULT_DONE;
        return SQLOOT_RESULT_DONE;
    }

    // If ORDER BY present, collect all results first
    if (pStmt->pOrderBy && pStmt->dwOrderByCount > 0 && !pStmt->bResultsBuffered)
    {
        // Collect all results
        if (pStmt->dwTableCount == 1)
        {
            while (NextTableRow(pStmt->pDb, &pStmt->Tables[0]))
            {
                if (pStmt->pWhereExpr && !EvaluateCondition(pStmt, pStmt->pWhereExpr))
                    continue;

                if (!BuildResultRow(pStmt))
                    continue;

                AddRowToBuffer(pStmt, &pStmt->ResultRow);
            }
        }
        else
        {
            // JOIN case (with optional rowid seek optimization)
            if (!pStmt->bInitialized)
            {
                if (!NextTableRow(pStmt->pDb, &pStmt->Tables[0]))
                    goto _DONE_COLLECTING;
                pStmt->bInitialized = TRUE;
                for (i = 1; i < pStmt->dwTableCount; i++)
                    ResetTableIterator(&pStmt->Tables[i]);
                for (i = 0; i < pStmt->dwJoinCount; i++)
                {
                    pStmt->Joins[i].bHadMatch = FALSE;
                    pStmt->Joins[i].bSeekDone = FALSE;
                }
            }

            while (TRUE)
            {
                BOOL bAdvanced = FALSE;

                for (i = pStmt->dwTableCount - 1; i >= 1; i--)
                {
                    PSQLOOT_JOIN pJoin = &pStmt->Joins[i - 1];

                    if (pJoin->bUseRowIdSeek)
                    {
                        if (!pJoin->bSeekDone)
                        {
                            PSQLOOT_VALUE pKeyVal = NULL;

                            pJoin->bSeekDone = TRUE;
                            pKeyVal = GetColumnValue(pStmt, &pJoin->SeekKeyColRef);

                            if (pKeyVal && pKeyVal->nType >= SQLOOT_TYPE_INT8 && pKeyVal->nType <= SQLOOT_TYPE_ONE)
                            {
                                if (SeekTableRowById(pStmt->pDb, &pStmt->Tables[i], pKeyVal->i64Value))
                                {
                                    bAdvanced = TRUE;
                                    pJoin->bHadMatch = TRUE;

                                    {
                                        DWORD j = 0;
                                        for (j = i + 1; j < pStmt->dwTableCount; j++)
                                        {
                                            ResetTableIterator(&pStmt->Tables[j]);
                                            if (j <= pStmt->dwJoinCount)
                                            {
                                                pStmt->Joins[j - 1].bHadMatch = FALSE;
                                                pStmt->Joins[j - 1].bSeekDone = FALSE;
                                            }
                                        }
                                    }

                                    break;
                                }
                            }

                            pStmt->Tables[i].bHasRow = FALSE;
                            pJoin->bHadMatch = FALSE;
                        }
                        else
                        {
                            pStmt->Tables[i].bHasRow = FALSE;
                            pJoin->bSeekDone = FALSE;
                            pJoin->bHadMatch = FALSE;
                        }
                    }
                    else
                    {
                        if (NextTableRow(pStmt->pDb, &pStmt->Tables[i]))
                        {
                            bAdvanced = TRUE;
                            BOOL bCondMatch = TRUE;

                            if (pJoin->pOnCondition)
                                bCondMatch = EvaluateCondition(pStmt, pJoin->pOnCondition);

                            if (!bCondMatch)
                            {
                                i++;
                                continue;
                            }

                            {
                                DWORD j = 0;
                                for (j = i + 1; j < pStmt->dwTableCount; j++)
                                    ResetTableIterator(&pStmt->Tables[j]);
                            }
                            break;
                        }
                        else
                        {
                            ResetTableIterator(&pStmt->Tables[i]);
                        }
                    }
                }

                if (!bAdvanced)
                {
                    if (!NextTableRow(pStmt->pDb, &pStmt->Tables[0]))
                        goto _DONE_COLLECTING;
                    for (i = 1; i < pStmt->dwTableCount; i++)
                        ResetTableIterator(&pStmt->Tables[i]);
                    for (i = 0; i < pStmt->dwJoinCount; i++)
                    {
                        pStmt->Joins[i].bHadMatch = FALSE;
                        pStmt->Joins[i].bSeekDone = FALSE;
                    }
                    continue;
                }

                {
                    BOOL bAllHaveRows = TRUE;
                    for (i = 0; i < pStmt->dwTableCount; i++)
                    {
                        if (!pStmt->Tables[i].bHasRow)
                        {
                            bAllHaveRows = FALSE;
                            break;
                        }
                    }

                    if (!bAllHaveRows)
                        continue;
                }

                if (pStmt->pWhereExpr && !EvaluateCondition(pStmt, pStmt->pWhereExpr))
                    continue;

                if (BuildResultRow(pStmt))
                    AddRowToBuffer(pStmt, &pStmt->ResultRow);
            }
        }

_DONE_COLLECTING:
        SortResultBuffer(pStmt);
        pStmt->bResultsBuffered = TRUE;
        pStmt->dwCurrentResultIndex = 0;

        return SQLootStep(pStmt);
    }

    FreeRow(&pStmt->ResultRow);
    pStmt->bHasRow = FALSE;

    // Simple case: single table, no JOINs
    if (pStmt->dwTableCount == 1)
    {
        while (NextTableRow(pStmt->pDb, &pStmt->Tables[0]))
        {
            if (pStmt->pWhereExpr)
            {
                if (!EvaluateCondition(pStmt, pStmt->pWhereExpr))
                    continue;
            }

            if (!BuildResultRow(pStmt))
                continue;

            pStmt->dwRowsReturned++;
            pStmt->bHasRow = TRUE;
            pStmt->nLastResult = SQLOOT_RESULT_ROW;
            return SQLOOT_RESULT_ROW;
        }

        pStmt->nLastResult = SQLOOT_RESULT_DONE;
        return SQLOOT_RESULT_DONE;
    }

    // JOIN case: nested loop INNER JOIN (with optional rowid seek optimization)
    if (!pStmt->bInitialized)
    {
        if (!NextTableRow(pStmt->pDb, &pStmt->Tables[0]))
        {
            pStmt->nLastResult = SQLOOT_RESULT_DONE;
            return SQLOOT_RESULT_DONE;
        }
        pStmt->bInitialized = TRUE;

        for (i = 1; i < pStmt->dwTableCount; i++)
            ResetTableIterator(&pStmt->Tables[i]);
        for (i = 0; i < pStmt->dwJoinCount; i++)
        {
            pStmt->Joins[i].bHadMatch = FALSE;
            pStmt->Joins[i].bSeekDone = FALSE;
        }
    }

    while (TRUE)
    {
        BOOL bAdvanced = FALSE;

        for (i = pStmt->dwTableCount - 1; i >= 1; i--)
        {
            PSQLOOT_JOIN pJoin = &pStmt->Joins[i - 1];

            if (pJoin->bUseRowIdSeek)
            {
                // Rowid seek optimization: O(log n) instead of O(n)
                if (!pJoin->bSeekDone)
                {
                    PSQLOOT_VALUE pKeyVal = NULL;

                    pJoin->bSeekDone = TRUE;
                    pKeyVal = GetColumnValue(pStmt, &pJoin->SeekKeyColRef);

                    if (pKeyVal && pKeyVal->nType >= SQLOOT_TYPE_INT8 && pKeyVal->nType <= SQLOOT_TYPE_ONE)
                    {
                        if (SeekTableRowById(pStmt->pDb, &pStmt->Tables[i], pKeyVal->i64Value))
                        {
                            bAdvanced = TRUE;
                            pJoin->bHadMatch = TRUE;

                            {
                                DWORD j = 0;
                                for (j = i + 1; j < pStmt->dwTableCount; j++)
                                {
                                    ResetTableIterator(&pStmt->Tables[j]);
                                    if (j <= pStmt->dwJoinCount)
                                    {
                                        pStmt->Joins[j - 1].bHadMatch = FALSE;
                                        pStmt->Joins[j - 1].bSeekDone = FALSE;
                                    }
                                }
                            }

                            break;
                        }
                    }

                    // Seek failed — no match for this outer row
                    pStmt->Tables[i].bHasRow = FALSE;
                    pJoin->bHadMatch = FALSE;
                }
                else
                {
                    // Already sought for this outer row — only one match possible with rowid
                    pStmt->Tables[i].bHasRow = FALSE;
                    pJoin->bSeekDone = FALSE;
                    pJoin->bHadMatch = FALSE;
                }
            }
            else
            {
                // Original path: full table scan
                if (NextTableRow(pStmt->pDb, &pStmt->Tables[i]))
                {
                    bAdvanced = TRUE;

                    BOOL bCondMatch = TRUE;
                    if (pJoin->pOnCondition)
                        bCondMatch = EvaluateCondition(pStmt, pJoin->pOnCondition);

                    if (!bCondMatch)
                    {
                        i++;
                        continue;
                    }

                    pJoin->bHadMatch = TRUE;

                    {
                        DWORD j = 0;
                        for (j = i + 1; j < pStmt->dwTableCount; j++)
                        {
                            ResetTableIterator(&pStmt->Tables[j]);
                            if (j <= pStmt->dwJoinCount)
                                pStmt->Joins[j - 1].bHadMatch = FALSE;
                        }
                    }

                    break;
                }
                else
                {
                    ResetTableIterator(&pStmt->Tables[i]);
                    pJoin->bHadMatch = FALSE;
                }
            }
        }

        if (!bAdvanced)
        {
            if (!NextTableRow(pStmt->pDb, &pStmt->Tables[0]))
            {
                pStmt->nLastResult = SQLOOT_RESULT_DONE;
                return SQLOOT_RESULT_DONE;
            }

            for (i = 1; i < pStmt->dwTableCount; i++)
                ResetTableIterator(&pStmt->Tables[i]);
            for (i = 0; i < pStmt->dwJoinCount; i++)
            {
                pStmt->Joins[i].bHadMatch = FALSE;
                pStmt->Joins[i].bSeekDone = FALSE;
            }

            continue;
        }

        // Check if all tables have rows
        {
            BOOL bAllHaveRows = TRUE;
            for (i = 0; i < pStmt->dwTableCount; i++)
            {
                if (!pStmt->Tables[i].bHasRow)
                {
                    bAllHaveRows = FALSE;
                    break;
                }
            }

            if (!bAllHaveRows)
                continue;
        }

        // Check WHERE
        if (pStmt->pWhereExpr)
        {
            if (!EvaluateCondition(pStmt, pStmt->pWhereExpr))
                continue;
        }

        if (!BuildResultRow(pStmt))
            continue;

        pStmt->dwRowsReturned++;
        pStmt->bHasRow = TRUE;
        pStmt->nLastResult = SQLOOT_RESULT_ROW;
        return SQLOOT_RESULT_ROW;
    }
}

INT SQLootFinalize(IN PSQLOOT_STMT pStmt)
{
    DWORD i = 0;

    if (!pStmt)
        return SQLOOT_RESULT_ERROR;

    if (pStmt->pSelectColumns)
        HeapFree(GetProcessHeap(), 0, pStmt->pSelectColumns);

    if (pStmt->pWhereExpr)
        FreeExpression(pStmt->pWhereExpr);

    if (pStmt->pOrderBy)
    {
        for (i = 0; i < pStmt->dwOrderByCount; i++)
        {
            if (pStmt->pOrderBy[i].Expr.pFuncArg)
                FreeExpression(pStmt->pOrderBy[i].Expr.pFuncArg);
        }
        HeapFree(GetProcessHeap(), 0, pStmt->pOrderBy);
    }

    if (pStmt->pResultBuffer)
    {
        for (i = 0; i < pStmt->dwResultBufferCount; i++)
            FreeRow(&pStmt->pResultBuffer[i]);
        HeapFree(GetProcessHeap(), 0, pStmt->pResultBuffer);
    }

    for (i = 0; i < pStmt->dwJoinCount; i++)
    {
        if (pStmt->Joins[i].pOnCondition)
            FreeExpression(pStmt->Joins[i].pOnCondition);
    }

    for (i = 0; i < pStmt->dwTableCount; i++)
        FreeTableRef(&pStmt->Tables[i]);

    FreeRow(&pStmt->ResultRow);

    HeapFree(GetProcessHeap(), 0, pStmt);
    return SQLOOT_RESULT_OK;
}

#pragma endregion

// ===============================================================================================================================================================
// COLUMN ACCESS FUNCTIONS
// ===============================================================================================================================================================

#pragma region COLUMN_ACCESS

INT SQLootColumnCount(IN PSQLOOT_STMT pStmt)
{
    if (!pStmt || !pStmt->bHasRow)
        return 0;
    return (INT)pStmt->ResultRow.dwColumnCount;
}

INT SQLootColumnType(IN PSQLOOT_STMT pStmt, IN INT nCol)
{
    PSQLOOT_VALUE pValue = NULL;

    if (!pStmt || !pStmt->bHasRow || nCol < 0 || (DWORD)nCol >= pStmt->ResultRow.dwColumnCount)
        return SQLOOT_COLTYPE_NULL;

    pValue = &pStmt->ResultRow.pColumns[nCol].Value;

    if (pValue->nType == SQLOOT_TYPE_NULL)
        return SQLOOT_COLTYPE_NULL;
    else if (pValue->nType >= SQLOOT_TYPE_INT8 && pValue->nType <= SQLOOT_TYPE_ONE)
        return SQLOOT_COLTYPE_INTEGER;
    else if (pValue->nType == SQLOOT_TYPE_FLOAT64)
        return SQLOOT_COLTYPE_FLOAT;
    else if (pValue->nType == SQLOOT_TYPE_TEXT)
        return SQLOOT_COLTYPE_TEXT;
    else if (pValue->nType == SQLOOT_TYPE_BLOB)
        return SQLOOT_COLTYPE_BLOB;

    return SQLOOT_COLTYPE_NULL;
}

INT64 SQLootColumnInt64(IN PSQLOOT_STMT pStmt, IN INT nCol)
{
    PSQLOOT_VALUE pValue = NULL;

    if (!pStmt || !pStmt->bHasRow || nCol < 0 || (DWORD)nCol >= pStmt->ResultRow.dwColumnCount)
        return 0;

    pValue = &pStmt->ResultRow.pColumns[nCol].Value;

    if (pValue->nType >= SQLOOT_TYPE_INT8 && pValue->nType <= SQLOOT_TYPE_ONE)
        return pValue->i64Value;
    else if (pValue->nType == SQLOOT_TYPE_FLOAT64)
        return (INT64)pValue->dValue;
    else if (pValue->nType == SQLOOT_TYPE_TEXT && pValue->Text.pszData)
        return _atoi64(pValue->Text.pszData);

    return 0;
}

INT SQLootColumnInt(IN PSQLOOT_STMT pStmt, IN INT nCol)
{
    return (INT)SQLootColumnInt64(pStmt, nCol);
}

DOUBLE SQLootColumnDouble(IN PSQLOOT_STMT pStmt, IN INT nCol)
{
    PSQLOOT_VALUE pValue = NULL;

    if (!pStmt || !pStmt->bHasRow || nCol < 0 || (DWORD)nCol >= pStmt->ResultRow.dwColumnCount)
        return 0.0;

    pValue = &pStmt->ResultRow.pColumns[nCol].Value;

    if (pValue->nType == SQLOOT_TYPE_FLOAT64)
        return pValue->dValue;
    else if (pValue->nType >= SQLOOT_TYPE_INT8 && pValue->nType <= SQLOOT_TYPE_ONE)
        return (DOUBLE)pValue->i64Value;

    return 0.0;
}

LPCSTR SQLootColumnText(IN PSQLOOT_STMT pStmt, IN INT nCol)
{
    PSQLOOT_VALUE pValue = NULL;

    if (!pStmt || !pStmt->bHasRow || nCol < 0 || (DWORD)nCol >= pStmt->ResultRow.dwColumnCount)
        return NULL;

    pValue = &pStmt->ResultRow.pColumns[nCol].Value;

    if (pValue->nType == SQLOOT_TYPE_TEXT)
        return pValue->Text.pszData;

    return NULL;
}

CONST PBYTE SQLootColumnBlob(IN PSQLOOT_STMT pStmt, IN INT nCol)
{
    PSQLOOT_VALUE pValue = NULL;

    if (!pStmt || !pStmt->bHasRow || nCol < 0 || (DWORD)nCol >= pStmt->ResultRow.dwColumnCount)
        return NULL;

    pValue = &pStmt->ResultRow.pColumns[nCol].Value;

    if (pValue->nType == SQLOOT_TYPE_BLOB)
        return pValue->Blob.pbData;
    else if (pValue->nType == SQLOOT_TYPE_TEXT)
        return (PBYTE)pValue->Text.pszData;

    return NULL;
}

INT SQLootColumnBytes(IN PSQLOOT_STMT pStmt, IN INT nCol)
{
    PSQLOOT_VALUE pValue = NULL;

    if (!pStmt || !pStmt->bHasRow || nCol < 0 || (DWORD)nCol >= pStmt->ResultRow.dwColumnCount)
        return 0;

    pValue = &pStmt->ResultRow.pColumns[nCol].Value;

    if (pValue->nType == SQLOOT_TYPE_BLOB)
        return (INT)pValue->Blob.dwSize;
    else if (pValue->nType == SQLOOT_TYPE_TEXT)
        return (INT)pValue->Text.dwLength;

    return 0;
}

#pragma endregion

// ===============================================================================================================================================================
// DEBUGGING
// ===============================================================================================================================================================

LPCSTR SQLootErrmsg(IN PSQLOOT_DB pDb)
{
#ifdef SQLOOT_DEBUG
    UNREFERENCED_PARAMETER(pDb);
    return g_szErrorMsg;
#else
    return "";
#endif 

}

