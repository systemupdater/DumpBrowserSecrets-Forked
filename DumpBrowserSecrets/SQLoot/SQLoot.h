// Parses SQLite3 file format (https://www.sqlite.org/fileformat.html)
// Supports: SELECT cols, INNER JOIN, WHERE (=, >, IS NOT NULL, AND), ORDER BY (with length()), table aliases, string/int literals
// Replacing 'sqlite-amalgamation-3510100' from < v1.1.1
#ifndef SQLOOT_H
#define SQLOOT_H

#include <Windows.h>
#include <Shlwapi.h>
#include <Strsafe.h>

#pragma comment(lib, "Shlwapi.lib")


// ===============================================================================================================================================================
// CONFIGURATIONS CONSTANTS
// ===============================================================================================================================================================

#define SQLOOT_DEBUG

// ===============================================================================================================================================================
// CONSTANTS
// ===============================================================================================================================================================

#define SQLOOT_MAGIC                "SQLite format 3"
#define SQLOOT_HEADER_SIZE          100
#define SQLOOT_MASTER_ROOT_PAGE     1

// Page types
#define SQLOOT_PAGE_INTERIOR_INDEX  0x02
#define SQLOOT_PAGE_INTERIOR_TABLE  0x05
#define SQLOOT_PAGE_LEAF_INDEX      0x0A
#define SQLOOT_PAGE_LEAF_TABLE      0x0D

// Text encodings
#define SQLOOT_UTF8                 1
#define SQLOOT_UTF16LE              2
#define SQLOOT_UTF16BE              3

// Column types
#define SQLOOT_TYPE_NULL            0
#define SQLOOT_TYPE_INT8            1
#define SQLOOT_TYPE_INT16           2
#define SQLOOT_TYPE_INT24           3
#define SQLOOT_TYPE_INT32           4
#define SQLOOT_TYPE_INT48           5
#define SQLOOT_TYPE_INT64           6
#define SQLOOT_TYPE_FLOAT64         7
#define SQLOOT_TYPE_ZERO            8
#define SQLOOT_TYPE_ONE             9
#define SQLOOT_TYPE_BLOB            10
#define SQLOOT_TYPE_TEXT            11

// Result codes
#define SQLOOT_RESULT_OK            0
#define SQLOOT_RESULT_ERROR         1
#define SQLOOT_RESULT_NOTADB        26
#define SQLOOT_RESULT_ROW           100
#define SQLOOT_RESULT_DONE          101

// Open flags
#define SQLOOT_OPEN_READONLY        0x00000001

// Max limits
#define SQLOOT_MAX_COLUMNS          256
#define SQLOOT_MAX_SQL_LENGTH       8192
#define SQLOOT_MAX_TABLE_NAME       128
#define SQLOOT_MAX_TABLES           16

// Expression types
#define SQLOOT_EXPR_COLUMN          1
#define SQLOOT_EXPR_INTEGER         2
#define SQLOOT_EXPR_FLOAT           3
#define SQLOOT_EXPR_STRING          4
#define SQLOOT_EXPR_NULL            5
#define SQLOOT_EXPR_COMPARE         6
#define SQLOOT_EXPR_AND             7
#define SQLOOT_EXPR_IS_NULL         9
#define SQLOOT_EXPR_IS_NOT_NULL     10
#define SQLOOT_EXPR_FUNCTION        12

// Comparison operators
#define SQLOOT_CMP_EQ               1   // =
#define SQLOOT_CMP_NE               2   // != or <>
#define SQLOOT_CMP_LT               3   // <
#define SQLOOT_CMP_LE               4   // <=
#define SQLOOT_CMP_GT               5   // >
#define SQLOOT_CMP_GE               6   // >=

// Function types
#define SQLOOT_FUNC_LENGTH          1

// Sort order
#define SQLOOT_SORT_ASC             0
#define SQLOOT_SORT_DESC            1

// Join types
#define SQLOOT_JOIN_INNER           0

// ===============================================================================================================================================================
// STRUCTURES
// ===============================================================================================================================================================

typedef struct _SQLOOT_HEADER {
    CHAR    szMagic[16];
    WORD    wPageSize;
    BYTE    bWriteVersion;
    BYTE    bReadVersion;
    BYTE    bReservedSpace;
    BYTE    bMaxPayloadFrac;
    BYTE    bMinPayloadFrac;
    BYTE    bLeafPayloadFrac;
    DWORD   dwFileChangeCounter;
    DWORD   dwDatabaseSizePages;
    DWORD   dwFirstFreelistPage;
    DWORD   dwFreelistPageCount;
    DWORD   dwSchemaCookie;
    DWORD   dwSchemaFormat;
    DWORD   dwDefaultCacheSize;
    DWORD   dwLargestRootPage;
    DWORD   dwTextEncoding;
    DWORD   dwUserVersion;
    DWORD   dwIncrementalVacuum;
    DWORD   dwApplicationId;
    BYTE    bReserved[20];
    DWORD   dwVersionValidFor;
    DWORD   dwSqliteVersion;
} SQLOOT_HEADER, *PSQLOOT_HEADER;

typedef struct _SQLOOT_VALUE {
    INT     nType;
    union {
        INT64   i64Value;
        DOUBLE  dValue;
        struct {
            PBYTE   pbData;
            DWORD   dwSize;
        } Blob;
        struct {
            LPSTR   pszData;
            DWORD   dwLength;
        } Text;
    };
} SQLOOT_VALUE, *PSQLOOT_VALUE;

typedef struct _SQLOOT_COLUMN {
    CHAR            szName[SQLOOT_MAX_TABLE_NAME];
    SQLOOT_VALUE    Value;
} SQLOOT_COLUMN, *PSQLOOT_COLUMN;

typedef struct _SQLOOT_ROW {
    INT64           i64RowId;
    DWORD           dwColumnCount;
    PSQLOOT_COLUMN  pColumns;
} SQLOOT_ROW, *PSQLOOT_ROW;

typedef struct _SQLOOT_TABLE_INFO {
    CHAR    szType[32];
    CHAR    szName[SQLOOT_MAX_TABLE_NAME];
    CHAR    szTableName[SQLOOT_MAX_TABLE_NAME];
    DWORD   dwRootPage;
    LPSTR   pszSql;
    CHAR    szColumnNames[SQLOOT_MAX_COLUMNS][SQLOOT_MAX_TABLE_NAME];
    DWORD   dwSchemaColumnCount;
} SQLOOT_TABLE_INFO, *PSQLOOT_TABLE_INFO;

typedef struct _SQLOOT_DB {
    PBYTE               pbFileData;
    DWORD               dwFileSize;
    DWORD               dwPageSize;
    DWORD               dwPageCount;
    DWORD               dwTextEncoding;
    DWORD               dwSchemaFormat;
    PSQLOOT_TABLE_INFO  pTables;
    DWORD               dwTableCount;
    BOOL                bIsOpen;
} SQLOOT_DB, *PSQLOOT_DB;

typedef struct _SQLOOT_CELL {
    INT64   i64RowId;
    PBYTE   pbPayload;
    DWORD   dwPayloadSize;
    DWORD   dwLeftChildPage;
} SQLOOT_CELL, *PSQLOOT_CELL;

typedef struct _SQLOOT_PAGE_INFO {
    BYTE    bPageType;
    WORD    wFirstFreeblock;
    WORD    wCellCount;
    WORD    wCellContentStart;
    BYTE    bFragmentedFreeBytes;
    DWORD   dwRightMostPointer;
} SQLOOT_PAGE_INFO, *PSQLOOT_PAGE_INFO;

// Column reference (e.g., "b.title" or "title")
typedef struct _SQLOOT_COLUMN_REF {
    CHAR    szAlias[SQLOOT_MAX_TABLE_NAME];      // Table alias (e.g., "b") or empty
    CHAR    szColumn[SQLOOT_MAX_TABLE_NAME];     // Column name (e.g., "title")
    INT     nTableIndex;                         // Resolved table index
    INT     nColumnIndex;                        // Resolved column index
} SQLOOT_COLUMN_REF, *PSQLOOT_COLUMN_REF;

// Expression node for WHERE clause
typedef struct _SQLOOT_EXPR 
{
    INT     nType;                              // SQLOOT_EXPR_*
    INT     nOperator;                          // SQLOOT_CMP_* for comparisons

    // For column reference
    SQLOOT_COLUMN_REF   ColRef;

    // For literal values
    INT64               i64Value;
    DOUBLE              dValue;
    CHAR                szStrValue[512];

    // For function calls
    INT                 nFuncType;              // SQLOOT_FUNC_*
    struct _SQLOOT_EXPR* pFuncArg;              // Function argument

    // For binary operations (AND, comparison)
    struct _SQLOOT_EXPR* pLeft;
    struct _SQLOOT_EXPR* pRight;
} SQLOOT_EXPR, *PSQLOOT_EXPR;

// ORDER BY item
typedef struct _SQLOOT_ORDER_BY {
    SQLOOT_EXPR     Expr;                       // Can be column or function
    INT             nSortOrder;                 // SQLOOT_SORT_ASC or SQLOOT_SORT_DESC
} SQLOOT_ORDER_BY, *PSQLOOT_ORDER_BY;

// Table reference with alias
typedef struct _SQLOOT_TABLE_REF {
    CHAR    szTableName[SQLOOT_MAX_TABLE_NAME];
    CHAR    szAlias[SQLOOT_MAX_TABLE_NAME];
    DWORD   dwRootPage;
    DWORD   dwTableInfoIndex;

    // Iterator state for this table
    PDWORD  pdwPageStack;
    PDWORD  pdwCellIndexStack;
    DWORD   dwStackDepth;
    DWORD   dwStackCapacity;

    // Current row from this table
    SQLOOT_ROW      CurrentRow;
    SQLOOT_VALUE    RowIdValue;
    BOOL            bHasRow;
    BOOL            bEndOfTable;
} SQLOOT_TABLE_REF, *PSQLOOT_TABLE_REF;

// Column type categories (returned by SQLootColumnType)
typedef enum _SQLOOT_COLTYPE {
    SQLOOT_COLTYPE_NULL = 1,
    SQLOOT_COLTYPE_INTEGER = 2,
    SQLOOT_COLTYPE_FLOAT = 3,
    SQLOOT_COLTYPE_TEXT = 4,
    SQLOOT_COLTYPE_BLOB = 5
} SQLOOT_COLTYPE;

// JOIN info
typedef struct _SQLOOT_JOIN {
    INT             nRightTableIndex;           // Index in table refs array
    INT             nJoinType;                  // SQLOOT_JOIN_INNER
    PSQLOOT_EXPR    pOnCondition;               // ON clause expression
    BOOL            bHadMatch;

    // Rowid seek optimization (O(log n) instead of O(n) per outer row)
    BOOL            bUseRowIdSeek;              // TRUE if we can B-tree seek instead of scan
    BOOL            bSeekDone;                  // Have we sought for the current outer row?
    SQLOOT_COLUMN_REF   SeekKeyColRef;          // Column providing the rowid value to seek
} SQLOOT_JOIN, *PSQLOOT_JOIN;

// SELECT column
typedef struct _SQLOOT_SELECT_COL {
    SQLOOT_COLUMN_REF   ColRef;
    CHAR                szOutputName[SQLOOT_MAX_TABLE_NAME];
} SQLOOT_SELECT_COL, *PSQLOOT_SELECT_COL;

typedef struct _SQLOOT_STMT {
    PSQLOOT_DB          pDb;

    // Tables involved (first is FROM, rest are JOINs)
    SQLOOT_TABLE_REF    Tables[SQLOOT_MAX_TABLES];
    DWORD               dwTableCount;

    // JOIN conditions
    SQLOOT_JOIN         Joins[SQLOOT_MAX_TABLES - 1];
    DWORD               dwJoinCount;

    // SELECT columns
    PSQLOOT_SELECT_COL  pSelectColumns;
    DWORD               dwSelectColumnCount;

    // WHERE clause
    PSQLOOT_EXPR        pWhereExpr;

    // ORDER BY clause
    PSQLOOT_ORDER_BY    pOrderBy;
    DWORD               dwOrderByCount;

    // Result buffering (for ORDER BY)
    PSQLOOT_ROW         pResultBuffer;
    DWORD               dwResultBufferCount;
    DWORD               dwResultBufferCapacity;
    DWORD               dwCurrentResultIndex;
    BOOL                bResultsBuffered;

    // Combined result row
    SQLOOT_ROW          ResultRow;
    BOOL                bHasRow;
    INT                 nLastResult;
    DWORD               dwRowsReturned;

    // Iteration state
    BOOL                bInitialized;
} SQLOOT_STMT, *PSQLOOT_STMT;


// ===============================================================================================================================================================
// FUNCTION DECLARATIONS
// ===============================================================================================================================================================

#ifdef __cplusplus
extern "C" {
#endif

    // Main API functions
    INT SQLootOpen(IN LPCSTR pszFilePath, OUT PSQLOOT_DB* ppDb, IN INT nFlags);
    INT SQLootPrepare(IN PSQLOOT_DB pDb, IN LPCSTR pszSql, IN INT nSqlLength, OUT PSQLOOT_STMT* ppStmt);
    INT SQLootStep(IN PSQLOOT_STMT pStmt);
    INT SQLootFinalize(IN PSQLOOT_STMT pStmt);
    INT SQLootClose(IN PSQLOOT_DB pDb);

    // Column access functions
    INT SQLootColumnCount(IN PSQLOOT_STMT pStmt);
    INT SQLootColumnType(IN PSQLOOT_STMT pStmt, IN INT nCol);
    INT64 SQLootColumnInt64(IN PSQLOOT_STMT pStmt, IN INT nCol);
    INT SQLootColumnInt(IN PSQLOOT_STMT pStmt, IN INT nCol);
    DOUBLE SQLootColumnDouble(IN PSQLOOT_STMT pStmt, IN INT nCol);
    LPCSTR SQLootColumnText(IN PSQLOOT_STMT pStmt, IN INT nCol);
    CONST PBYTE SQLootColumnBlob(IN PSQLOOT_STMT pStmt, IN INT nCol);
    INT SQLootColumnBytes(IN PSQLOOT_STMT pStmt, IN INT nCol);

    // Debugging
    LPCSTR SQLootErrmsg(IN PSQLOOT_DB pDb);

#ifdef __cplusplus
}
#endif

#endif // SQLOOT_H