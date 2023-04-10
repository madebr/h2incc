#include "h2incc.h"
#include "incfile.h"
#include "list.h"
#include "util.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <conio.h>
#include <windows.h>
#endif

#define STRINGPOOLMAX       0x1000000       // address space reserved for string pool
#define STRINGPOOLSIZE      0x10000
#define MAXWARNINGLVL       3               // max value for -Wn switch

struct StringLL {
    struct StringLL* next;                  // next in linked list chain
    char* str;                              // string value
};

// global vars

int g_argc;
char** g_argv;
char** g_envp;

uint32_t g_rc;
char* g_pszFilespec;                        // filespec cmdline param
char* g_pszIniPath;                         // -C cmdline ini path
char* g_pszOutDir;                          // -O cmdline output directory
char* g_pszOutFileName;                     // -o cmdline output filename
char* g_pszIncDir;                          // -I cmdline include directory
uint32_t g_dwStructSuffix;                  // number used for nameless structures
uint32_t g_dwDefCallConv;                   // default calling convention
struct StringLL* g_pInpFiles;               // linked list of processed input files
struct LIST* g_pStructures;                 // list of structures defined in current file
struct LIST* g_pStructureTags;              // list of struct typedefs defined in current file
struct LIST* g_pMacros;                     // list of macros defined in current file
#if PROTOSUMMARY
struct LIST* g_pPrototypes;                 // list of prototypes
#endif
#if TYPEDEFSUMMARY
struct LIST* g_pTypedefs;                   // list of typedefs
#endif
#if DYNPROTOQUALS
struct LIST* g_pQualifiers;                 // list of prototype qualifiers
#endif

struct SORTARRAY g_ReservedWords;       // profile file strings [Reserved Words]
struct SORTARRAY g_KnownStructures;     // profile file strings
struct SORTARRAY g_ProtoQualifiers;     // profile file strings
char** g_ppSimpleTypes;                 // profile file strings [Simple Types]
struct ITEM_MACROINFO* g_ppKnownMacros; // profile file strings [Macro Names]
struct ITEM_STRSTR* g_ppTypeAttrConv;   // profile file strings
struct ITEM_STRSTR* g_ppConvertTokens;  // profile file strings
struct ITEM_STRSTR* g_ppConvertTypes1;  // profile file strings
struct ITEM_STRSTR* g_ppConvertTypes2;  // profile file strings
struct ITEM_STRSTR* g_ppConvertTypes3;  // profile file strings
struct ITEM_STRSTR* g_ppAlignments;     // profile file strings
struct ITEM_STRINT* g_ppTypeSize;       // profile file strings

uint8_t g_bTerminate;                   // 1=terminate app as soon as possible

uint8_t g_bAddAlign;                    // -a cmdline switch
uint8_t g_bBatchmode;                   // -b cmdline switch
uint8_t g_bIncludeComments;             // -c cmdline switch
uint8_t g_bAssumeDllImport;             // -d cmdline switch
uint8_t g_bUseDefProto;                 // -D cmdline switch
uint8_t g_bCreateDefs;                  // -e cmdline switch
uint8_t g_bIgnoreDllImport;             // -g cmdline switch
uint8_t g_bProcessInclude;              // -i cmdline switch
uint8_t g_bUntypedMembers;              // -m cmdline switch

#if PROTOSUMMARY
uint8_t g_bProtoSummary;                // -p cmdline switch
#endif
uint8_t g_bNoRecords;                   // -q cmdline switch
uint8_t g_bRecordsInUnions;             // -r cmdline switch
uint8_t g_bSummary;                     // -S cmdline switch
#if TYPEDEFSUMMARY
uint8_t g_bTypedefSummary;              // -t cmdline switch
#endif
uint8_t g_bUntypedParams;               // -u cmdline switch
uint8_t g_bVerbose;                     // -v cmdline switch
uint8_t g_bWarningLevel;                // -W cmdline switch
#ifdef OVERWRITE_PROTECTION
uint8_t g_bOverwrite;                   // -y cmdline switch
#endif
uint8_t g_b64bit;

uint8_t g_bIniPathExpected;              // temp var for -C cmdline switch
#ifdef OUTPUTDIRECTORY_ARG
uint8_t g_bOutDirExpected;              // temp var for -O cmdline switch
#endif
uint8_t g_bOutFileNameExpected;         // temp var for -o cmdline switch
uint8_t g_bSelExpected;                 // temp var for -s cmdline switch
uint8_t g_bCallConvExpected;            // temp var for -k cmdline switch
uint8_t g_bIncDirExpected;              // temp var for -k cmdline switch

uint8_t g_bPrototypes = 1;              // modified by -s cmdline switch
uint8_t g_bTypedefs = 1;                // modified by -s cmdline switch
uint8_t g_bConstants = 1;               // modified by -s cmdline switch
uint8_t g_bExternals = 1;               // modified by -s cmdline switch

#ifdef _TRACE
int debug_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int res = vfprintf(stderr, format, args);
    va_end(args);
    return res;
}
#endif

// table for sections to read from h2incc.ini

struct CONVTABENTRY {
    char* pszSection;                   // section name
    void* pPtr;                         // pointer to table pointer or SORTARRAY
    void* pDefault;                     // pointer to default value table
    size_t itemSize;                    // size of one item
    uint32_t dwFlags;                   //
    void* pStorage;
};

enum {
    CF_ATOL = 0x0001,                   // convert every 2. string to a number
    CF_SORT = 0x0002,                   // sort table (then pPtr must point to a SORTARRAY)
    CF_CASE	= 0x0004,                   // strings are case-insensitive
    CF_KEYS = 0x0008,                   // data is key-only
};

// token conversion
// use with care

struct ITEM_STRSTR g_ConvertTokensDefault[] = {
    { "interface", "struct" },
    { 0 },
};

// known type attribute names
// usually not used, since defined in h2incc.ini

struct ITEM_STRSTR g_TypeAttrConvDefault[] = {
    { "*far", "" },
    { "*near", "" },
    { "IN", "" },
    { "OUT","" },
    { 0 },
};

// known macro names
// usually not used, since defined in h2incc.ini

struct ITEM_MACROINFO g_KnownMacrosDefault[] = {
    { "DECLARE_HANDLE", 0, 0, NULL, NULL },
    { "DECLARE_GUID", 0, 0, NULL, NULL },
    { 0 },
};

// known structure names
// usually not used, since defined in h2incc.ini

char* g_KnownStructuresDefaultElems[] = {
        "POINT",
        NULL,
};

struct SORTARRAY g_KnownStructuresDefault = {
        g_KnownStructuresDefaultElems,
        0,
};

// structure sizes. required if a structure is a parameter
// with size > 4

struct ITEM_STRINT g_TypeSizeDefault[] = {
    { "CY",         8 },
    { "DATE",       8 },
    { "DOUBLE",     8 },
    { "POINT",      8 },
    { "VARIANT",    16 },
    { 0 },
};

// known prototype qualifier names
// usually not used, since defined in h2incc.ini

struct PROTOQUAL {
    char* pszName;
    uint32_t  dwValue;
};

struct PROTOQUAL g_ProtoQualifiersDefaultElems[] = {
    { "__cdecl",    FQ_CDECL    },
    { "_cdecl",     FQ_CDECL    },
    { "__stdcall",  FQ_STDCALL  },
    { "_stdcall",   FQ_STDCALL  },
    { "stdcall",    FQ_STDCALL  },
    { "WINAPI",     FQ_STDCALL  },
    { "WINAPIV",    FQ_CDECL    },
    { "APIENTRY",   FQ_STDCALL  },
    { "__inline",   FQ_INLINE   },
    { 0 },
};

struct SORTARRAY g_ProtoQualifiersDefault = {
    (char**)g_ProtoQualifiersDefaultElems,
    0,
};

// simple types default
// usually not used, since defined in h2incc.ini

char* g_SimpleTypesDefault[] = {
    "BYTE",
    "SBYTE",
    "WORD",
    "SWORD",
    "DWORD",
    "SDWORD",
    "QWORD",
    "LONG",
    "ULONG",
    "REAL4",
    "REAL8",
    "BOOL",
    "CHAR",
    "ptr",
    "PVOID",
    "WCHAR",
    "WPARAM",
    "LPARAM",
    "LRESULT",
    "HANDLE",
    "HINSTANCE",
    "HGLOBAL",
    "HLOCAL",
    "HWND",
    "HMENU",
    "HDC",
    NULL,
};

// type conversion 1 default
// usually not used since defined in h2incc.ini

struct ITEM_STRSTR g_ConvertTypes1Default[] = {
    { "DWORDLONG",  "QWORD" },
    { "ULONGLONG",  "QWORD" },
    { "LONGLONG",   "QWORD" },
    { "double",     "REAL8" },
    { 0 },
};


// type conversion 2 default
// usually not used since defined in h2incc.ini

struct ITEM_STRSTR g_ConvertTypes2Default[] = {
    { "int",            "SDWORD" },
    { "unsigned int",   "DWORD" },
    { "short",          "SWORD" },
    { "unsigned short", "WORD" },
    { "long",           "SDWORD" },
    { "unsigned long",  "DWORD" },
    { "char",           "SBYTE" },
    { "unsigned char",  "BYTE" },
    { "wchar_t",        "WORD" },
    { "LPCSTR",         "LPSTR" },
    { "LPCWSTR",        "LPWSTR" },
    { "UINT",           "DWORD" },
    { "ULONG",          "DWORD" },
    { "LONG",           "SDWORD" },
    { "FLOAT",          "REAL4" },
    { 0 },
};

// type conversion 3 default
// usually not used since defined in h2incc.ini

struct ITEM_STRSTR g_ConvertTypes3Default[] = {
    { "POINT",      "QWORD" },
    { "VARIANT",    "VARIANT" },
    { 0 },
};

// structure alignments default
// usually not used since defined in h2incc.ini

struct ITEM_STRSTR g_AlignmentsDefault[] = {
    { 0 },
};

// reserved words default
// usually not used since defined in h2incc.ini

char* g_ReservedWordsDefaultElems[] = {
    "cx",
    "dx",
    NULL,
};

struct SORTARRAY g_ReservedWordsDefault = {
    g_ReservedWordsDefaultElems,
    0,
};

// default tables marked with CF_ATOL, CF_SORT or CF_CASE must be in .data

struct CONVTABENTRY convtab[] = {
    { "Simple Type Names",          &g_ppSimpleTypes,   &g_SimpleTypesDefault,      sizeof(struct NAMEITEM),        CF_KEYS ,                       NULL },
    { "Macro Names",                &g_ppKnownMacros,   &g_KnownMacrosDefault,      sizeof(struct ITEM_MACROINFO),  CF_ATOL ,                       NULL },
    { "Structure Names",            &g_KnownStructures, &g_KnownStructuresDefault,  sizeof(char *),                 CF_SORT | CF_KEYS,              NULL },
    { "Reserved Words",             &g_ReservedWords,   &g_ReservedWordsDefault,    sizeof(char *),                 CF_CASE | CF_SORT | CF_KEYS,    NULL },
    { "Type Qualifier Conversion",  &g_ppTypeAttrConv,  &g_TypeAttrConvDefault,     sizeof(struct ITEM_STRSTR),     0,                              NULL },
    { "Type Conversion 1",          &g_ppConvertTypes1, &g_ConvertTypes1Default,    sizeof(struct ITEM_STRSTR),     0,                              NULL },
    { "Type Conversion 2",          &g_ppConvertTypes2, &g_ConvertTypes2Default,    sizeof(struct ITEM_STRSTR),     0,                              NULL },
    { "Type Conversion 3",          &g_ppConvertTypes3, &g_ConvertTypes3Default,    sizeof(struct ITEM_STRSTR),     0,                              NULL },
    { "Token Conversion",           &g_ppConvertTokens, &g_ConvertTokensDefault,    sizeof(struct ITEM_STRSTR),     0,                              NULL },
    { "Prototype Qualifiers",       &g_ProtoQualifiers, &g_ProtoQualifiersDefault,  sizeof(struct ITEM_STRINT),     CF_ATOL | CF_SORT,              NULL },
    { "Alignment",                  &g_ppAlignments,    &g_AlignmentsDefault,       sizeof(struct ITEM_STRINT),     0,                        NULL },
    { "Type Size",                  &g_ppTypeSize,      &g_TypeSizeDefault,         sizeof(struct ITEM_STRINT),     CF_ATOL,                        NULL },
    { 0 },
};

// command line switch table

struct CLSWITCH {
    char bSwitch;
    uint8_t bType;
    uint8_t* pVoid;
};

#define CLS_ISBOOL  1
#define CLS_ISPROC  2   // not used

struct CLSWITCH clswitchtab[] = {
    { 'a',  CLS_ISBOOL, &g_bAddAlign },
    { 'b',  CLS_ISBOOL, &g_bBatchmode },
    { 'c',  CLS_ISBOOL, &g_bIncludeComments },
    { 'C',  CLS_ISBOOL, &g_bIniPathExpected },
//  { 'd',  CLS_ISBOOL, &g_bAssumeDllImport },
//  { 'D',  CLS_ISBOOL, &g_bUseDefProto },
//  { 'g',  CLS_ISBOOL, &g_bIgnoreDllImport },
    { 'e',  CLS_ISBOOL, &g_bCreateDefs },
    { 'i',  CLS_ISBOOL, &g_bProcessInclude },
    { 'I',  CLS_ISBOOL, &g_bIncDirExpected },
    { 'k',  CLS_ISBOOL, &g_bCallConvExpected },
//  { 'm',  CLS_ISBOOL, &g_bUntypedMembers },
#ifdef OUTPUTDIRECTORY_ARG
    { 'O',  CLS_ISBOOL, &g_bOutDirExpected },
#endif
    { 'o',  CLS_ISBOOL, &g_bOutFileNameExpected },
#if PROTOSUMMARY
    { 'p',  CLS_ISBOOL, &g_bProtoSummary },
#endif
    { 'q',  CLS_ISBOOL, &g_bNoRecords },
    { 'r',  CLS_ISBOOL, &g_bRecordsInUnions },
    { 's',  CLS_ISBOOL, &g_bSelExpected },
    { 'S',  CLS_ISBOOL, &g_bSummary },
#if TYPEDEFSUMMARY
    { 't',  CLS_ISBOOL, &g_bTypedefSummary },
#endif
    { 'u',  CLS_ISBOOL, &g_bUntypedParams },
    { 'v',  CLS_ISBOOL, &g_bVerbose },
    { 'x',  CLS_ISBOOL, &g_b64bit },
#ifdef OVERWRITE_PROTECTION
    { 'y',  CLS_ISBOOL, &g_bOverwrite },
#endif
    { 0 },
};


#define Summary1 "  -S: print summary (structures, macros"
#if PROTOSUMMARY
#define Summary2 ", prototypes"
#else
#define Summary2
#endif
#if TYPEDEFSUMMARY
#define Summary3 ", typedefs"
#else
#define Summary3
#endif
#define Summary4 ")\n"
#define SummaryStr Summary1 Summary2 Summary3 Summary4


char* szUsage =
    "h2incd " VERSION ", " COPYRIGHT "\n"
    "usage: h2incd <options> filespec\n"
    "  -a: add @align to STRUCT declarations\n"
    "  -b: batch mode, no user interaction\n"
    "  -c: include comments in output\n"
    "  -C: path to ini config file\n"
    "  -d0|1|2|3: define __declspec(dllimport) handling:\n"
    "     0: [default] decide depending on values in h2incc.ini\n"
    "     1: always assume __declspec(dllimport) is set\n"
    "     2: always assume __declspec(dllimport) is not set\n"
    "     3: if possible use @DefProto macro to define prototypes\n"
    "  -e: write full decorated names of function prototypes to a .DEF file\n"
    "  -i: process #include lines\n"
    "  -I directory: specify an additionally directory to search for header files\n"
    "  -k c|s|p|y: set default calling convention for prototypes\n"
#ifdef OUTPUTDIRECTORY_ARG
"  -O directory: set output directory (default is current dir)\n"
#endif
"  -o output: set output file name (default is stem of input file with .inc suffix)\n"
#if PROTOSUMMARY
    "  -p: print prototypes in summary\n"
#endif
    "  -q: avoid RECORD definitions\n"
    "  -r: create size-safe RECORD definitions\n"
    "  -s c|p|t|e: selective output, c/onstants,p/rototypes,t/ypedefs,e/xternals\n"
    SummaryStr
#if TYPEDEFSUMMARY
    "  -t: print typedefs in summary\n"
#endif
    "  -u: generate untyped parameters (DWORDs) in prototypes\n"
    "  -v: verbose mode\n"
    "  -W0|1|2|3: set warning level (default is 0)\n"
    "  -x: assume 64-bit (default = 32-bit)\n"
#ifdef OVERWRITE_PROTECTION
    "  -y: overwrite existing .INC files without confirmation\n"
#endif
;

char g_szDrive[4];
char g_szDir[256];
char g_szName[256];
char g_szExt[256];

char* AddString(char* pszString) {
    size_t stringSize = strlen(pszString) + 1;
    size_t allocSize = stringSize;
    char* data = malloc(allocSize);
    strcpy(data, pszString);
    return data;
}

void DestroyString(char* pszString) {
    free(pszString);
}

// scan command line for options

int getoption(char* pszArgument) {
    if (pszArgument[0] == '-') {
        if (pszArgument[2] != '\0') {
            if (pszArgument[1] == 'W') {
                uint8_t val = pszArgument[2] - '0';
                if (val > MAXWARNINGLVL) {
                    return 1;
                }
                g_bWarningLevel = val;
                return 0;
            } else if (pszArgument[1] == 'd') {
                uint8_t val = pszArgument[2] - '0';
                switch (val) {
                case 0:
                    break;
                case 1:
                    g_bAssumeDllImport = 1;
                    break;
                case 2:
                    g_bIgnoreDllImport = 1;
                    break;
                case 3:
                    g_bUseDefProto = 1;
                    break;
                default:
                    return 1;
                }
                return 0;
            }
            return 1;
        } else {
            for (size_t i = 0; i < ARRAY_SIZE(clswitchtab); i++) {
                if (clswitchtab[i].bSwitch == pszArgument[1]) {
                    if (clswitchtab[i].bType == CLS_ISBOOL) {
                        *clswitchtab[i].pVoid = 1;
                    }
                    return 0;
                }
            }
            return 1;
        }
    } else {
        if (g_bIniPathExpected) {
            g_pszIniPath = pszArgument;
            g_bIniPathExpected = 0;
        } else if (g_bOutFileNameExpected) {
            g_pszOutFileName = pszArgument;
            g_bOutFileNameExpected = 0;
#ifdef OUTPUTDIRECTORY_ARG
        } else if (g_bOutDirExpected) {
            g_pszOutDir = pszArgument;
            g_bOutDirExpected = 0;
#endif
        } else if (g_bSelExpected) {
            g_bConstants = 0;
            g_bTypedefs = 0;
            g_bPrototypes = 0;
            g_bExternals = 0;
            for (size_t i = 0; pszArgument[i] != '\0'; i++) {
                switch (pszArgument[i]) {
                case 'c':
                    g_bConstants = 1;
                    break;
                case 'e':
                    g_bExternals = 1;
                    break;
                case 'p':
                    g_bPrototypes = 1;
                    break;
                case 't':
                    g_bTypedefs = 1;
                    break;
                default:
                    return 1;
                }
            }
            g_bSelExpected = 0;
        } else if (g_bCallConvExpected) {
            if (pszArgument[1] != '\0') {
                return 1;
            }
            switch (pszArgument[0]) {
            case 'c':
                g_dwDefCallConv |= FQ_CDECL;
                break;
            case 's':
                g_dwDefCallConv |= FQ_STDCALL;
                break;
            case 'p':
                g_dwDefCallConv |= FQ_PASCAL;
                break;
            case 'y':
                g_dwDefCallConv |= FQ_SYSCALL;
                break;
            default:
                return 1;
            }
            g_bCallConvExpected = 0;
        } else  if (g_bIncDirExpected) {
            g_pszIncDir = pszArgument;
            g_bIncDirExpected = 0;
        } else {
            char* prevFileSpec = g_pszFilespec;
            g_pszFilespec = pszArgument;
            if (prevFileSpec != NULL) {
                return 1;
            }
        }
    }
    return 0;
}

// profile file access procs

char* xstrtok(char* str, char* delim, char* match) {
    char c;
    while (1) {
        c = *str;
        for (int i = 0; 1; i++) {
            if (c == delim[i]) {
                *match = c;
                return str;
            }
            if (delim[i] == '\0') {
                break;
            }
        }
        str++;
    }
    *match = '\0';
    return NULL;
}

// load all strings from a section

size_t LoadStrings(char* pszTypes, char** pTable, char* stringTable, int bKeyOnly, size_t *pTextLength, size_t itemSize) {
    size_t nbStrings = 0;
    size_t textLength = 0;
    char **currentItem = pTable;
    while (1) {
        pTable = currentItem;
        if (*pszTypes == '\0' || *pszTypes == '[') {
            break;
        }
        if (*pszTypes <=  ' ' || *pszTypes == ';') {
            pszTypes++;
            while (*pszTypes != '\0' && (*pszTypes != '\n' && *pszTypes != '\r')) {
                pszTypes++;
            }
            while (*pszTypes == '\n' || *pszTypes == '\r') {
                pszTypes++;
            }
            continue;
        }
        char match;
        char* nextStr = xstrtok(pszTypes, bKeyOnly ? "\r\n" : "=\r\n", &match);
        int remaining = (size_t)itemSize;
        size_t lenKey = nextStr - pszTypes;
        nbStrings++;
        textLength += lenKey + 1;
        if (pTable != NULL) {
            strncpy(stringTable, pszTypes, lenKey);
            stringTable[lenKey] = '\0';
            *pTable = stringTable;
            stringTable += lenKey + 1;
            pTable++;
            remaining -= sizeof(char*);
        }
        pszTypes = nextStr;
        while (*pszTypes != '\0' && (*pszTypes == '\n' || *pszTypes == '\r')) {
            pszTypes++;
        }
        if (match == '=') {
            pszTypes++;
            assert(!bKeyOnly);
            nextStr = xstrtok(pszTypes, "\r\n", &match);
            size_t lenVal = nextStr - pszTypes;
            nbStrings++;
            textLength += lenVal + 1;
            if (pTable != NULL) {
                // pszTypes[0] = '\0';
                strncpy(stringTable, pszTypes, lenVal);
                stringTable[lenVal] = '\0';
                *pTable = stringTable;
                stringTable += lenVal + 1;
                pTable++;
                remaining -= sizeof(char*);
            }
            pszTypes = nextStr;
            while (*pszTypes != '\0' && (*pszTypes == '\n' || *pszTypes == '\r')) {
                pszTypes++;
            }
        }
        assert(remaining >= 0);
        if (pTable) {
            currentItem = (char**)((char*)currentItem + itemSize);
        }
    }
    debug_printf("LoadStrings()=%u stringsize=%u\n", (unsigned)nbStrings, (unsigned)textLength);
    *pTextLength = textLength;
    return nbStrings;
}

// find a section in a profile file (h2incc.ini)

char* FindSection(const char* pszSection, char* pszFile, size_t dwSize) {
    size_t dwStrSize = strlen(pszSection);
    char* result = NULL;
    while (dwSize != 0) {
        if (*pszFile == '[') {
            pszFile++;
            dwSize--;
            if (strncmp(pszFile, pszSection, dwStrSize) == 0) {
                if (pszFile[dwStrSize] == ']') {
                    pszFile += dwStrSize;
                    while (*pszFile != '\0' && *pszFile != '\n') {
                        pszFile++;
                        dwSize--;
                    }
                    if (*pszFile == '\n') {
                        result = pszFile + 1;
                        break;
                    }
                }
            }
        }
        if (dwSize != 0) {
            dwSize--;
            pszFile++;
        }
        while (*pszFile != '\n' && *pszFile != '\0') {
            pszFile++;
            dwSize--;
        }
        while (*pszFile == '\n') {
            pszFile++;
            dwSize--;
        }
    }
    debug_printf("FindSection(%s)=%p\n", pszSection, result);
    return result;
}

// load strings from various sections in a profile

void LoadTablesFromProfile(char* pszInput, size_t dwSize) {
    char* stringSpace;

    for (struct CONVTABENTRY *tabEntry = convtab; tabEntry->pszSection != NULL; tabEntry++) {
        debug_printf("%s\n", tabEntry->pszSection);
        char* start = NULL;
        if (pszInput != NULL) {
            start = FindSection(tabEntry->pszSection, pszInput, dwSize);
        }
        if (start != NULL) {
            size_t textLength;
            size_t nb = LoadStrings(start, NULL, NULL, tabEntry->dwFlags & CF_KEYS, &textLength, tabEntry->itemSize);
            if (nb != 0) {
                char* textBuffer = malloc(textLength);
                tabEntry->pStorage = textBuffer;
                *(char***)tabEntry->pPtr = malloc((nb + 1) * tabEntry->itemSize);
                memset(*(char**)tabEntry->pPtr, 0, (nb + 1) * tabEntry->itemSize);
                if (tabEntry->pPtr != NULL) {
                    LoadStrings(start, *(char***)tabEntry->pPtr, textBuffer, tabEntry->dwFlags & CF_KEYS, &textLength, tabEntry->itemSize);
                }
            }
        } else {
            *(char***)tabEntry->pPtr = (char**)tabEntry->pDefault;
        }
    }
}

void FreeProfileData(void) {
    for (struct CONVTABENTRY *tabEntry = convtab; tabEntry->pszSection != NULL; tabEntry++) {
        free(tabEntry->pStorage);
    }
}

char* ReadIniFile(char* szIniPath, size_t* pSize) {
    char* cwd;
    FILE* f;
    size_t dwSize;
    char* pContents;

    f = fopen(szIniPath, "r");
    if (f == NULL) {
        if (g_bVerbose) {
            fprintf(stderr, "profile file %s not found, using defaults!\n", szIniPath);
        }
        *pSize = 0;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    dwSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    pContents = malloc(dwSize + 1);
    if (pContents == NULL) {
        fprintf(stderr, "out of memory reading profile file\n");
        *pSize = 0;
        return NULL;
    }
    fread(pContents, 1, dwSize, f);
    fclose(f);
    pContents[dwSize] = '\0';
    *pSize = dwSize;
    return pContents;
}

#ifdef OVERWRITE_PROTECTION
// check if output file would be overwritten
// if yes, optionally ask user how to proceed
// returns: 1 -> proceed, 0 -> skip processing

int CheckIncFile(char* pszOutName, char* pszFileName, struct INCFILE* pParent) {
    char szPrefix[MAX_PATH+32];

    if (g_bOverwrite) {
        return 1;
    }
    FILE* f = fopen(pszOutName, "r");
    if (f == NULL) {
        return 1;
    }
    fclose(f);
    if (g_bBatchmode) {
        if (g_bWarningLevel >= MAXWARNINGLVL || pParent == NULL) {
            if (pParent == NULL) {
                szPrefix[0] = '\0';
            } else {
                uint32_t line;
                char* pFileName = GetFileNameIncFile(pParent, &line);
                sprintf(szPrefix, "%s, %u: ", pFileName, line);
            }
            fprintf(stderr, "%s%s exists, file %s not processed\n", szPrefix, pszOutName, pszFileName);
        }
        return 0;
    }
    fprintf(stderr, "%s exists, overwrite (y/n)?", pszOutName);
    while (1) {
        int c = fgetc(stdin);
        if (c >= 'A') {
            c |= 0x20; // cheap tolower
        }
        if (c == 'y' || c == 'n' || c == '\x03') {
            break;
        }
        fprintf(stderr, "\n");
        if (c == 3) {
            g_bTerminate = 1;
            fprintf(stderr, "^C");
            return 0;
        } else if (c == 'n') {
            fprintf(stderr, "%s not processed\n", pszFileName);
            return 0;
        }
    }
    return 1;
}
#endif

static void InputFileNameToIncFileName(const char* inputFilePath, const char* outputDirectory, char* outputFilePath) {
    const char* fileName = inputFilePath;

    if (g_pszOutFileName != NULL) {
        strcpy(outputFilePath, g_pszOutFileName);
    } else {
#if 0
        while (fileName != NULL) {
            if (*fileName == '\0') {
                break;
            }
            char* next = strpbrk(fileName, "/\\");
            if (next == NULL) {
                break;
            }
            fileName = next + 1;
        }
        strcpy(outputFilePath, outputDirectory);
        strcat(outputFilePath, "/");

        const char *suffixPos = fileName;
        while (fileName != NULL) {
            const char *next = strpbrk(*suffixPos == '.' ? suffixPos + 1 : suffixPos, ".");
            if (next == NULL) {
                break;
            }
            suffixPos = next;
        }
        size_t nameLen;
        if (suffixPos == fileName) {
            nameLen = strlen(fileName);
        } else {
            nameLen = suffixPos - fileName;
        }
        strncat(outputFilePath, fileName, nameLen);
        strcat(outputFilePath, ".inc");
#else
        outputFilePath[0] = '\0';
#endif
    }
}

// process 1 header file

int ProcessFile(char* pszFileName, struct INCFILE* pParent) {
    struct INCFILE* pIncFile;
    char* lpFilePart;
    // char szFileName[MAX_PATH];
    char szOutName[MAX_PATH];
    int res;

    // don't process files more than once
    struct StringLL *pCurrent = g_pInpFiles;
    while (pCurrent != NULL) {
        pCurrent = pCurrent->next;
        if (strcasecmp(pCurrent->str, pszFileName) == 0) {
            return 0;
        }
    }
    struct StringLL* pNew = malloc(sizeof(struct StringLL) + strlen(pszFileName) + 1);
    pNew->str = (char*)pNew + sizeof(struct StringLL);
    strcpy(pNew->str, pszFileName);
    pNew->next = g_pInpFiles;
    g_pInpFiles = pNew;

    if (g_bVerbose) {
        if (pParent != NULL) {
            uint32_t line;
            char* name = GetFileNameIncFile(pParent, &line);
            fprintf(stderr, "%s, %u: ", name, line);
        }
        fprintf(stderr, "file '%s'\n", pszFileName);
    }
    InputFileNameToIncFileName(pszFileName, g_pszOutDir, szOutName);
    debug_printf("%s => '%s'\n", pszFileName, szOutName);
    // _splitpath(szFileName, NULL, NULL, g_szName, g_szExt);
    // _makepath(szOutName, NULL, g_pszOutDir, g_szName, ".INC");

#ifdef OVERWRITE_PROTECTION
    if (!CheckIncFile(szOutName, pszFileName, pParent)) {
        return 0;
    }
#endif
    pIncFile = CreateIncFile(pszFileName, pParent);
    if (pIncFile == NULL) {
        return 0;
    }
    ParserIncFile(pIncFile);
    AnalyzerIncFile(pIncFile);
    res = WriteIncFile(pIncFile, szOutName);
    //WriteDefIncFile(pIncFile, szOutName);
    DestroyIncFile(pIncFile);
    return res;
}


char* strlwr(char* s) {
    for (char* p = s; *p != '\0'; p++) {
        *p = tolower(*p);
    }
    return s;
}


// some tables contain strings expected to be numerical values
// converted them here

union strint_strstr {
    struct ITEM_STRINT strint;
    struct ITEM_STRSTR strstr;
};

void ConvertTables() {
    for (struct CONVTABENTRY* tabEntry = convtab; tabEntry->pszSection != NULL; tabEntry++) {
        if (tabEntry->dwFlags & CF_ATOL) {
            if (*(char***)tabEntry->pPtr != (char**)tabEntry->pDefault) {
                union strint_strstr* pPtr = *(union strint_strstr**)tabEntry->pPtr;
                while (pPtr->strstr.key != NULL) {
                    pPtr->strint.value = atol(pPtr->strstr.value);
                    pPtr++;
                }
            }
        }
        int inc;
        if (tabEntry->dwFlags & CF_ATOL) {
            inc = 2;
        } else {
            inc = 1;
        }
        // convert string to lower case
        if (tabEntry->dwFlags & CF_CASE && tabEntry->pPtr != tabEntry->pDefault) {
            char** pPtr = *(char***)tabEntry->pPtr;
            while (*pPtr != NULL) {
                strlwr(*pPtr);
                pPtr += inc;
            }
        }
        // sort string table
        if (tabEntry->dwFlags & CF_SORT) {
            uint32_t count = 0;
            char** pPtr = *(char***)tabEntry->pPtr;;
            while (*pPtr != NULL) {
                count++;
                pPtr += inc;
            }
            ((struct SORTARRAY*)tabEntry->pPtr)->numItems = count;
            qsort(*(char***)tabEntry->pPtr, count, inc * sizeof(char*), cmpproc);
        }
    }
}

int PrintTable(struct LIST* pTable, char* pszFormatString) {
    uint32_t count;
    count = 0;
    if (pTable != NULL) {
        SortList(pTable);
        char* pItem = GetNextItemList(pTable, NULL);
        while (pItem != NULL) {
            count++;
            fprintf(stderr, pszFormatString, ((struct NAMEITEM*)pItem)->pszName, &pItem[1]);
            pItem = GetNextItemList(pTable, (void*)pItem);
        }
    }
    return count;
}

void PrintSummary(char* pszFileName) {
    uint32_t dwcntStruct;
    uint32_t dwcntMacro;
#if PROTOSUMMARY
    uint32_t dwcntProto;
#endif
#if TYPEDEFSUMMARY
    uint32_t dwcntTypedef;
#endif

    if (!g_bSummary) {
        return;
    }
    fprintf(stderr, "Summary %s:\n", pszFileName);
    dwcntStruct = PrintTable(g_pStructures, "structure: %s\n");
    fprintf(stderr, "\n");
    dwcntMacro = PrintTable(g_pMacros, "macro: %s\n");
#if PROTOSUMMARY
    if (g_bProtoSummary) {
        fprintf(stderr, "\n");
        dwcntProto = PrintTable(g_pPrototypes, "prototype: %s\n");
    }
#endif
#if TYPEDEFSUMMARY
    if (g_bTypedefSummary) {
        fprintf(stderr, "\n");
        dwcntTypedef = PrintTable(g_pTypedefs, "typedef: %s\n");
    }
#endif
#if DYNPROTOQUALS
    if (g_bUseDefProto) {
        fprintf(stderr, "\n");
        PrintTable(g_pQualifiers, "prototype qualifier: %s [%X]\n");
#endif
    }
    fprintf(stderr, "%u structures\n%u macros\n", dwcntStruct, dwcntMacro);
#if PROTOSUMMARY
    fprintf(stderr, "%u prototypes\n", dwcntProto);
#endif
#if TYPEDEFSUMMARY
    fprintf(stderr, "%u typedefs\n", dwcntTypedef);
#endif
}

void ProcessFiles(char* pszFileSpec) {
    void* hFFHandle;
    char* pFilePart;
    char szDir[MAX_PATH];
    char szInpDir[MAX_PATH];
    char szFileSpec[MAX_PATH];
#if 0
    WIN32_FIND_DATA fd;

    GetCurrentDirectory(MAX_PATH, szDir);
    GetFullPathName(pszFileSpec, MAX_PATH, szFileSpec, &pFilePart);
    _splitpath(szFileSpec, g_szDrive, g_szDir, g_szName, g_szExt);
    _makepath(szInpDir, g_szDrive, g_szDir, NULL, NULL);
    debug_printf("ProcessFiles: input dir=%s\n", szInpDir);
    int res = SetCurrentDirectory(szInpDir);
    debug_printf("ProcessFiles: SetCurrentDirectory()=%X\n", res);
    strcpy(szFileSpec, g_szName);
    strcat(szFileSpec, g_szExt);

    debug_printf("ProcessFiles(%s)\n", szFileSpec);

    hFFHandle  = FindFirstFile(szFileSpec, &fd);
    if (res == ERROR_FILE_NOT_FOUND) {
        fprintf(stderr, "no matching files found\n");
        goto exit;
    }
    do {
        if (g_bTerminate) {
            break;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(fd.cFileName, ".") == 0) {
                continue;
            } else if (strcmp(fd.cFileName, "..") == 0) {
                continue;
            }
            int c;
            if (g_bBatchmode) {
                c = 'y';
            } else {
                fprintf(stderr, "%s is a directory, process all files inside (y/n)?", fd.cFileName);
                while (1) {
                    c = getch();
                    if (c >= 'A') {
                        c |= 0x20;
                    }
                    if (c == 'y' || c == 'n' || c == 3) {
                        break;
                    }
                }
            }
            if (c == 3) {
                break;
            } else if (c == 'y') {
                strcpy(szFileSpec, fd.cFileName);
                strcat(szFileSpec, "/*.*");
                ProcessFiles(szFileSpec);
                continue;
            }
        }

        g_rc = !ProcessFile(fd.cFileName, NULL);
        PrintSummary(fd.cFileName);
#else
        g_rc = !ProcessFile(pszFileSpec, NULL);
        PrintSummary(pszFileSpec);
#endif

        DestroyAnalyzerData();
#if 0
    } while (FindNextFile(hFFHandle, &fd));
    FindClose(hFFHandle);
exit:
    SetCurrentDirectory(szDir);
#endif
}

// main
// reads profile file
// reads command line
// loops thru all header files calling ProcessFile

int main(int argc, char** argv, char** envp) {
    size_t dwSize;
    char* pIniContents;
    char* lpFilePart;
    char szOutDir[MAX_PATH];
    char* pszIniPath;

    g_argc = argc;
    g_argv = argv;
    g_envp = envp;

    g_rc = 1;

    for (int i = 1; i < argc; i++) {
        if (getoption(argv[i])) {
            goto main_er;
        }
    }

    // read h2incc.ini
    pIniContents = ReadIniFile(g_pszIniPath, &dwSize);
    LoadTablesFromProfile(pIniContents, dwSize);
    free(pIniContents);
    pIniContents = NULL;
    ConvertTables();
    if (g_pszFilespec == NULL) {
main_er:
        fprintf(stderr, "%s", szUsage);
        goto exit;
    }
    if (g_pszOutDir == NULL) {
        g_pszOutDir = ".";
    }

    ProcessFiles(g_pszFilespec);

exit:
    FreeProfileData();
    return g_rc;
}
