#include "incfile.h"
#include "list.h"
#include "h2incc.h"
#include "util.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_ANDMEAN
#include <windows.h>
#endif

#define IFISSTRUCT      1           // handle "interface" as "struct"

#define MAXSTRUCTNAME   128

#define MAXITEMS        0x10000	    // max. items in structure/macro list
#define ADDTERMNULL	    0           // add ",0" to string declarations
#define ADD50PERCENT	0		    // 1=buffer size 50% larger than file size
							        // 0=buffer size 100% larger than file size
#define USELOCALALLOC	0		    // 1=use LocalAlloc, 0=use _malloc()

struct INCFILE {
    char*           pszIn;                  // pointer input stream
    char*           pszOut;                 // pointer output stream
    char*           pszInStart;             // pointer to input start
    char*           pszOutStart;            // pointer to output start
    char*		    pBuffer1;               // buffer pointers in/out
    char*           pBuffer2;               // buffer pointers in/out
    uint32_t        dwBufSize;              // size of buffers
    struct LIST*    pDefs;                  // .DEF file content
    char*           pszFileName;            // file name
    char*           pszFullPath;            // full path
    char*           pszLastToken;           //
    char*           pszImpSpec;             //
    char*           pszCallConv;            //
    char*           pszEndMacro;            //
    char*           pszPrefix;              //
    char*           pszStructName;          // current struct/union/class name
    uint32_t        dwBlockLevel;           // block level where pszEndMacro becomes active
    uint32_t        dwQualifiers;           //
    uint32_t        dwLine;                 // current line
    uint32_t        dwEnumValue;            // counter for enums
    uint32_t        dwRecordNum;            // counter for records
    //uint32_t        dwDefCallConv;        // default calling convention
    uint32_t        dwErrors;               // errors occured in this file conversion
    uint32_t        dwWarnings;             // warnings occured in this file conversion
    uint32_t        dwBraces;               // count curled braces
    struct INCFILE* pParent;                // parent INCFILE (if any)
    struct tm       filetime;
    //FILETIME        filetime;               //
    uint8_t         bIfStack[MAXIFLEVEL+1]; // 'if' stack
    uint8_t         bIfLvl;                 // current 'if' level
    uint8_t         bSkipPP;                // >0=dont parse preprocessor lines in input stream
    uint8_t         bNewLine;               // last token was a PP_EOL
    uint8_t         bContinuation;          // preprocessor continuation line
    uint8_t         bComment;               // counter for "/*" and "*/" strings
    uint8_t         bDefinedMac;            // "defined" macro in output stream included
    uint8_t         bAlignMac;              // "@align" macro in output stream included
    uint8_t         bUseLastToken;          //
    uint8_t         bC;                     // extern "C" occured
    uint8_t         bIsClass;               // inside a class definition
    uint8_t         bIsInterface;           // inside an interface definition
};

int contains(char *needle, char **array, int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(needle, array[i]) == 0) {
            return 1;
        }
    }
    return 0;
}


char* GetNextToken(struct INCFILE* pIncFile);
char *GetNextTokenPP(struct INCFILE* pIncFile);

int getblock(struct INCFILE* pIncFile, char* pszStructName, uint32_t dwMode, char* pszParent);
int MacroInvocation(struct INCFILE* pIncFile, char* pszToken, struct ITEM_MACROINFO* pMacroInfo, int bWriteLF);
void ProcessFile(char*, struct INCFILE* pIncFile);
int ParseTypedefFunction(struct INCFILE* pIncFile, char* pszName, int bAcceptBody, char* pszParent);
int ParseTypedefFunctionPtr(struct INCFILE* pIncFile, char* pszParent, char**outPszName);
char* TranslateName(char* , char*, int *);


// types for getblock()
enum {
    DT_STANDARD = 0,            // struct/union
    DT_EXTERN	= 1,	        // extern
    DT_ENUM		= 2,	        // enum
};

//  parser output ctrl codes
enum {
    PP_MACRO	= 0x0e0,        // is a macro
    PP_EOL		= 0x0e1,        // end of line
    PP_COMMENT	= 0x0e2,        // comment token
    PP_IGNORE	= 0x0e3,        // ignore token
    PP_WEAKEOL	= 0x0e4,        // '\' at the end of preprocessor lines
};

// flag values in [Known Macros]
enum {
    MF_0001			= 0x001,	// reserved
    MF_SKIPBRACES	= 0x002,	// skip braces in macro call
    MF_COPYLINE		= 0x004,	// assume rest of line belongs to macro call
    MF_PARAMS		= 0x008,	// assume method parameters coming after macro
    MF_UNK			= 0x010,
    MF_ENDMACRO		= 0x020,	// end next block with a ???_END macro call
    MF_STRUCTBEG	= 0x040,	// macro begins a struct/union (MIDL_INTERFACE)
    MF_INTERFACEBEG = 0x080,	// add an "??Interface equ <xxx>" line
    MF_INTERFACEEND = 0x100,	// add an "??Interface equ <>" line
};

struct INPSTAT {
    char* pszIn;
    uint32_t dwLine;
    uint8_t bIfStack[MAXIFLEVEL];
    uint8_t bIfLvl;
    uint8_t bNewLine;
};

#define LINKEDLIST_ITEM_CAPACITY        32
struct LinkedList {
    struct LinkedList* next;
    uintptr_t items[LINKEDLIST_ITEM_CAPACITY];
    uint8_t this_size;
};

struct LinkedList* CreateLinkedList(void) {
    struct LinkedList* res = malloc(sizeof(struct LinkedList));
    res->next = NULL;
    res->this_size = 0;
    return res;
}

void DestroyLinkedList(struct LinkedList* list) {
    while (list != NULL) {
        struct LinkedList *next = list->next;
        free(list);
        list = next;
    }
}

void AddLinkedList(struct LinkedList* list, uintptr_t item) {
    while (list->this_size >= LINKEDLIST_ITEM_CAPACITY) {
        struct LinkedList *next = list->next;
        if (next == NULL) {
            next = CreateLinkedList();
            list->next = next;
            break;
        }
        list = next;
    }
    list->items[list->this_size] = item;
    list->this_size++;
}

uint32_t GetNumItemsLinkedList(struct LinkedList* list) {
    uint32_t count = 0;
    while (list != NULL) {
        count += list->this_size;
        list = list->next;
    }
    return count;
}

uintptr_t GetItemLinkedList(struct LinkedList* list, uint32_t index) {
    while (index >= list->this_size) {
        index -= list->this_size;
        list = list->next;
        if (list == NULL) {
            abort();
            return 0;
        }
    }
    return list->items[index];
}

void IsDefine(struct INCFILE*);
void IsInclude(struct INCFILE*);
void IsError(struct INCFILE*);
void IsPragma(struct INCFILE*);
void IsIf(struct INCFILE*);
void IsElIf(struct INCFILE*);
void IsElse(struct INCFILE*);
void IsEndif(struct INCFILE*);
void IsIfdef(struct INCFILE*);
void IsIfndef(struct INCFILE*);

void IsIfNP(struct INCFILE*);
void IsElIfNP(struct INCFILE*);
void IsElseNP(struct INCFILE*);
void IsEndifNP(struct INCFILE*);
void IsIfNP(struct INCFILE*);

// preprocessor command tab
// commands not listed here will be commented out

struct PPCMD {
    char* pszCmd;
    void(*pfnHandler)(struct INCFILE*);
};

struct PPCMD ppcmds[] = {
    { "define", IsDefine },
    { "include", IsInclude },
    { "error", IsError },
    { "pragma", IsPragma },
    { "if", IsIf },
    { "elif", IsElIf },
    { "else", IsElse },
    { "endif", IsEndif },
    { "ifdef", IsIfdef },
    { "ifndef", IsIfndef },
    { 0 },
};

struct PPCMD ppcmdsnp[] = {
    { "if", IsIfNP },
    { "elif", IsElIfNP },
    { "else", IsElseNP },
    { "endif", IsEndifNP },
    { "ifdef", IsIfNP },
    { "ifndef", IsIfNP },
    { 0 },
};

// operator conversion for #if/#elif expressions

struct OPCONV {
    char*       wOp;
    char*       pszSubst;
};

struct OPCONV g_szOpConvTab[] = {
    { "==", " eq " },
    { "!=", " ne " },
    { ">=", " ge " },
    { "<=", " le " },
	{ ">",  " gt " },
	{ "<",  " lt " },
    { "&&", " AND " },
    { "||", " OR " },
    { "!",  " 0 eq " },
};

#define sizeOpConvTab ARRAY_SIZE(g_szOpConvTab)

const char* szMac_defined =
    "ifndef defined\r\n"
    "defined macro x\r\n"
    "ifdef x\r\n"
    "  exitm <1>\r\n"
    "else\r\n"
    "  exitm <0>\r\n"
    "endif\r\n"
    "endm\r\n"
    "endif\r\n"
;

const char* szMac_align =
    "ifndef @align\r\n"
    "@align equ <>\r\n"
    "endif\r\n"
;

// delimiters known by parser

const char* bDelim = ",;:()[]{}|*<>!~-+=/&#";

// 2-byte opcodes known by parser

#define STR2UINT16(C1, C2) ((uint16_t)(((C1)<<8) | (C2)))
const uint16_t w2CharOps[] = {
    STR2UINT16('>', '>'),
    STR2UINT16('<', '<'),
    STR2UINT16('&', '&'),
    STR2UINT16('|', '|'),
    STR2UINT16('>', '='),
    STR2UINT16('<', '='),
    STR2UINT16('=', '='),
    STR2UINT16('!', '='),
    STR2UINT16('-', '>'),
    STR2UINT16(':', ':'),
    STR2UINT16('#', '#'),
    0,
};

char g_szComment[1024];
char g_szTemp[128];

void SaveInputStatus(struct INCFILE* pIncFile, struct INPSTAT* pStatus) {
    pStatus->pszIn      = pIncFile->pszIn;
    pStatus->dwLine     = pIncFile->dwLine;
    pStatus->bNewLine   = pIncFile->bNewLine;
    pStatus->bIfLvl     = pIncFile->bIfLvl;
    memcpy(pStatus->bIfStack, pIncFile->bIfStack, sizeof(pStatus->bIfStack));
}

void RestoreInputStatus(struct INCFILE* pIncFile, struct INPSTAT* pStatus) {
    pIncFile->pszIn     = pStatus->pszIn;
    pIncFile->dwLine    = pStatus->dwLine;
    pIncFile->bNewLine  = pStatus->bNewLine;
    pIncFile->bIfLvl    = pStatus->bIfLvl;
    memcpy(pIncFile->bIfStack, pStatus->bIfStack, sizeof(pIncFile->bIfStack));
}

// add an item to a list

void* InsertItem(struct INCFILE* pIncFile, struct LIST* pList, char* pszName) {
    char* s = AddString(pszName);
    if (s == NULL) {
        return NULL;
    }
    struct LISTITEM* pos = AddItemList(pList, s);
    if (pos == NULL) {
        fprintf(stderr, "%s, %u: out of symbol space\n", pIncFile->pszFileName, pIncFile->dwLine);
        pIncFile->dwErrors++;
        g_bTerminate = 1;
        return NULL;
    }
    return pos;
}

void* InsertStrIntItem(struct INCFILE* pIncFile, struct LIST* pList, char* pszName, uint32_t dwValue) {
    char* s = AddString(pszName);
    if (s == NULL) {
        return NULL;
    }
    struct LISTITEM* pos = AddItemList(pList, s);
    if (pos == NULL) {
        fprintf(stderr, "%s, %u: out of symbol space\n", pIncFile->pszFileName, pIncFile->dwLine);
        pIncFile->dwErrors++;
        g_bTerminate = 1;
        return NULL;
    }
    pos->value.u32 = dwValue;
    return pos;
}

void* InsertStrStrItem(struct INCFILE* pIncFile, struct LIST* pList, char* pszName, char* pszValue) {
    char* s = AddString(pszName);
    if (s == NULL) {
        return NULL;
    }
    char* v = AddString(pszValue);
    if (s == NULL) {
        return NULL;
    }
    struct LISTITEM* pos = AddItemList(pList, s);
    if (pos == NULL) {
        fprintf(stderr, "%s, %u: out of symbol space\n", pIncFile->pszFileName, pIncFile->dwLine);
        pIncFile->dwErrors++;
        g_bTerminate = 1;
        return NULL;
    }
    pos->value.pStr = pszValue;
    return pos;
}


// add a prototype to a list

char* InsertDefItem(struct INCFILE* pIncFile, char* pszFuncName, uint32_t dwParmBytes) {
    char szProto[MAX_PATH];

    if (pIncFile->dwQualifiers & FQ_STDCALL) {
        sprintf(szProto, "_%s@%u", pszFuncName, dwParmBytes);
    } else if (pIncFile->dwQualifiers & FQ_CDECL) {
        sprintf(szProto, "_%s", pszFuncName);
    } else {
        sprintf(szProto, "%s", pszFuncName);
    }
    char* s = AddString(szProto);
    if (s == NULL) {
        return NULL;
    }
    char* pos = AddItemList(pIncFile->pDefs, s);
    if (pos == NULL) {
        fprintf(stderr, "%s, %u: out of symbol space\n", pIncFile->pszFileName, pIncFile->dwLine);
        pIncFile->dwErrors++;
        g_bTerminate = 1;
        return NULL;
    }
    return pos;
}

// write a string to output stream
void write(struct INCFILE* pIncFile, const char* pszText) {
    strcpy(pIncFile->pszOut, pszText);
    pIncFile->pszOut += strlen(pszText);
}

int IsNewLine(struct INCFILE* pIncFile) {
    if (pIncFile->pszOut == pIncFile->pszOutStart) {
        return 1;
    }
    return pIncFile->pszOut[-1] == '\n';
}

void xprintf(struct INCFILE* pIncFile, const char* pszFormat, ...) {
    va_list args;
    va_start(args, pszFormat);
    int nb = vsprintf(pIncFile->pszOut, pszFormat, args);
    va_end(args);
    pIncFile->pszOut += nb;
}

int IsDelim(char c) {
    return strchr(bDelim, c) != NULL;
}

int IsTwoCharOp(char ch1, char ch2) {
    uint16_t wrd = STR2UINT16(ch1, ch2);
    for(int i = 0; w2CharOps[i] != 0; i++) {
        if (wrd == w2CharOps[i]) {
            return 1;
        }
    }
    return 0;
}

// translate tokens like "__export" or "__stdcall"
char* TranslateToken(char* pszType) {
    for (int i = 0; g_ppConvertTokens[i].key != NULL; i++) {
        if (strcmp(g_ppConvertTokens[i].key, pszType) == 0) {
            return g_ppConvertTokens[i].value;
        }
    }
    return pszType;
}

char* GetAlignment(char* pszStructure) {
    for (int i = 0; g_ppAlignments[i].key != NULL; i++) {
        if (strcmp(g_ppAlignments[i].key, pszStructure) == 0) {
            return g_ppAlignments[i].value;
        }
    }
    return NULL;
}

// get type sizes (for structures used as parameters)

int GetTypeSize(char* pszStructure) {
    for (int i = 0; g_ppTypeSize[i].key != NULL; i++) {
        if (strcmp(g_ppTypeSize[i].key, pszStructure) == 0) {
            return g_ppTypeSize[i].value;
        }
    }
    // FIXME: depends on arch: 32/64 bit
    return 4;
}

// convert type qualifiers

char* ConvertTypeQualifier(char* pszType) {
    for (int i = 0; g_ppTypeAttrConv[i].key != NULL; i++) {
        char* res;
        int cmpres;
        if (g_ppTypeAttrConv[i].key[0] == '*') {
            res = &g_ppTypeAttrConv[i].key[1];
            cmpres = stricmp(res, pszType);
        } else {
            res = g_ppTypeAttrConv[i].key;
            cmpres = strcmp(res, pszType);
        }
        if (cmpres == 0) {
            return res;
        }
    }
    return pszType;
}

char* TranslateType(char* pszType, uint32_t bMode) {
    for (int i = 0; g_ppConvertTypes1[i].key != NULL; i++) {
        if (strcmp(g_ppConvertTypes1[i].key, pszType) == 0) {
            return g_ppConvertTypes1[i].value;
        }
    }
    struct ITEM_STRSTR* lut = bMode ? g_ppConvertTypes3 : g_ppConvertTypes2;
    for (int i = 0; lut[i].key != NULL; i++) {
        if (strcmp(lut[i].key, pszType) == 0) {
            return lut[i].value;
        }
    }
    if (bMode) {
        return "DWORD";
    } else {
        return pszType;
    }
}

// check if a token is a simple type
// used by SkipCasts()

int IsSimpleType(char* pszType) {
    for (int i = 0; g_ppSimpleTypes[i] != NULL; i++) {
        if (strcmp(g_ppSimpleTypes[i], pszType) == 0) {
            return 1;
        }
    }
    return 0;
}

// check if a token is a structure

int IsStructure(char* pszType) {
    void* next;
    char* res = list_bsearch(pszType, g_KnownStructures.pItems, g_KnownStructures.numItems, sizeof(char*), cmpproc, &next);
    if (res == NULL) {
        res = FindItemList(g_pStructures, pszType);
    }
    return res != NULL;
}

int WriteComment(struct INCFILE* pIncFile) {
    if (g_bIncludeComments && g_szComment[1] != '\0') {
        write(pIncFile, g_szComment);
        g_szComment[1] = '\0';
        return 1;
    } else {
        return 0;
    }
}

void AddComment(char* pszToken) {
    strcpy(&g_szComment[1], &pszToken[1]);
    g_szComment[0] = ';';
}

// get next token (for preprocessor lines)

char* GetNextTokenPP(struct INCFILE* pIncFile) {
    while (1) {
        size_t len = strlen(pIncFile->pszIn);
        pIncFile->bNewLine = 0;
        if (len == 0) {
            return NULL;
        }
        char* current = pIncFile->pszIn;
        pIncFile->pszIn += len + 1;
        if (current[0] == (char)PP_EOL && current[1] == '\0') {
            pIncFile->dwLine++;
            pIncFile->bNewLine = 1;
            return NULL;
        } else if (current[0] == (char)PP_WEAKEOL && current[1] == '\0') {
            pIncFile->dwLine++;
            continue;
        } else if (current[0] == (char)PP_IGNORE) {
            continue;
        } else if (current[0] == (char)PP_COMMENT) {
            AddComment(current);
            continue;
        } else {
            return current;
        }
    }
}

// copy rest of line to destination

void CopyLine(struct INCFILE* pIncFile) {
    while (1) {
        char* next = GetNextTokenPP(pIncFile);
        if (next == NULL) {
            break;
        }
        write(pIncFile, next);
        write(pIncFile, " ");
    }
    WriteComment(pIncFile);
    write(pIncFile, "\r\n");
}

int IsName(struct INCFILE* pIncFile, char* pszType) {
    char c = *pszType;
    if (c >= 'A' && c <= 'Z') {
        return 1;
    } else if (c >= 'a' && c <= 'z') {
        return 1;
    } else if (c == '_' || c == '?' || c == '@') {
        return 1;
    } else if (c == '`' && pIncFile->bIsClass) {
        return 1;
    }
    return 0;
}

int IsAlpha(char c) {
    if (c == '?' || c == '@') {
        return 1;
    }
    if (c >= 'A' && c <= 'Z') {
        return 1;
    }
    if (c == '_') {
        return 1;
    }
    if (c >= 'a' && c <= 'z') {
        return 1;
    }
    return 0;
}

int IsAlphaNumeric(char c) {
    if (c >= '0' && c <= '9') {
        return 1;
    }
    return IsAlpha(c);
}

int IsNumOperator(char* c) {
    if (*c == '-' || *c == '+' || *c == '*' || *c == '/' || *c == '|' || *c == '&') {
        return 1;
    }
    if (strncmp(c, ">>", 2) == 0) {
        return 1;
    }
    if (strncmp(c, "<<", 2) == 0) {
        return 1;
    }
    return 0;
}

// translate operator in #define lines

char* TranslateOperator(char* pszToken) {
    if (strcmp(pszToken, ">>") == 0) {
        return " shr ";
    } else if (strcmp(pszToken, ">>") == 0) {
        return " shl ";
    } else if (strcmp(pszToken, "&") == 0) {
        return " and ";
    } else if (strcmp(pszToken, "|") == 0) {
        return " or ";
    } else if (strcmp(pszToken, "~") == 0) {
        return " not ";
    } else {
        return pszToken;
    }
}

// is current token a number (decimal or hexadecimal)

int IsNumber(char* pszInp) {
    return *pszInp >= '0' && *pszInp <= '9';
}

int IsReservedWord(char* pszName) {
    char szWord[64];

    strcpy(szWord, pszName);
    szWord[sizeof(szWord)-1] = '\0';
    for (size_t i = 0; i < strlen(szWord); i++) {
        szWord[i] = tolower(szWord[i]);
    }
    return list_bsearch(pszName, g_ReservedWords.pItems, g_ReservedWords.numItems, sizeof(char*), cmpproc, NULL) != NULL;
}

// if macro is found, return address of NAMEITEM
// or macro flags (then bit 0 of eax is set)

struct ITEM_MACROINFO* IsMacro(struct INCFILE* pIncFile, char* pszName) {
     for (int i = 0; g_ppKnownMacros[i].key != NULL; i++) {
         if (strcmp(g_ppKnownMacros[i].key, pszName) == 0) {
             return &g_ppKnownMacros[i];
         }
     }
     return FindItemList(g_pMacros, pszName);
}

// test if its a number enclosed in braces
void DeleteSimpleBraces(struct INCFILE* pIncFile) {
    struct INPSTAT sis;

    SaveInputStatus(pIncFile, &sis);
    char* firstT = GetNextTokenPP(pIncFile);
    if (firstT != NULL && *firstT == '(') {
        char* nextT = GetNextTokenPP(pIncFile);
        if (nextT != NULL) {
            if (*nextT == '-') {
                nextT == GetNextTokenPP(pIncFile);
                if (nextT == NULL) {
                    goto exit;
                }
            }
            if (*nextT >= '0' && *nextT <= '9') {
                char* lastT = GetNextTokenPP(pIncFile);
                if (lastT != NULL && *lastT == ')') {
                    *firstT = PP_IGNORE;
                    *lastT = PP_IGNORE;
                }
            }
        }
    }
exit:
    RestoreInputStatus(pIncFile, &sis);
}

// skip braces of "(" <number> ")" pattern

void SkipSimpleBraces(struct INCFILE* pIncFile) {
    struct INPSTAT sis;

    SaveInputStatus(pIncFile, &sis);
    while (1) {
        if (IsName(pIncFile, pIncFile->pszIn)) {
            if (GetNextTokenPP(pIncFile) == NULL) {
                break;
            }
        } else {
            DeleteSimpleBraces(pIncFile);
        }
        char* token = GetNextTokenPP(pIncFile);
        if (token == NULL) {
            break;
        }
    }
    RestoreInputStatus(pIncFile, &sis);
}

int IsString(char* pszToken) {
    if (*pszToken == '"') {
        return 1;
    }
    if (*pszToken >= '0' && *pszToken <= '9') {
        pszToken++;
        while (*pszToken != '\0') {
            if (*pszToken == ',') {
                return 1;
            }
            pszToken++;
        }
    }
    return 0;
}


void convertline_register_qualifier(struct INCFILE* pIncFile, char* pszName, int dwFlags) {
    if (dwFlags & (FQ_IMPORT | FQ_STDCALL | FQ_CDECL)) {
        struct LISTITEM* pQualListItem = FindItemList(g_pQualifiers, pszName);
        if (pQualListItem == NULL) {
            pQualListItem = InsertStrIntItem(pIncFile, g_pQualifiers, pszName, 0);
        }
        if (pQualListItem != NULL) {
            pQualListItem->value.u32 | dwFlags;
        } else {
        }
        debug_printf("qualifier %s attr %X added\n", pszName, dwFlags);
    }
}


// for EQU invocation
// called by IsDefine

void convertline(struct INCFILE* pIncFile, char* pszName) {
    int bExpression;
    char* pszValue;
    char* pszOut;
    struct LinkedList* ppszItems;
    uint32_t dwCnt;
    uint32_t dwEsp;
    uint32_t dwTmp[2];
    struct INPSTAT sis;

    ppszItems = NULL;
    pszValue = GetNextTokenPP(pIncFile);
    if (pszValue != NULL) {
        SaveInputStatus(pIncFile, &sis);
        bExpression = 0;
        char* token = pszValue;
        while (token != NULL) {
            if (IsString(token)) {
                bExpression = 0;
                break;
            }
            if (*token >= '0' && *token <= '9') {
                bExpression = 1;
            } else if (*token == '(' || *token == ')') {
            } else {
                if (IsAlpha(*token)) {
                    if (IsNumOperator(token)) {
                        bExpression = 0;
                        break;
                    } else {
                        bExpression = 1;
                    }
                }
            }
            token = GetNextTokenPP(pIncFile);
        }
        RestoreInputStatus(pIncFile, &sis);
        if (!bExpression) {
            write(pIncFile, "<");
        }
        pszOut = pIncFile->pszOut;
        ppszItems = CreateLinkedList();
        dwCnt = 0;
        while (pszValue != NULL) {
            AddLinkedList(ppszItems, (uintptr_t)pszValue);
            if (dwCnt != 0) {
                write(pIncFile, " ");
            }
            debug_printf("%u: item %s found\n", pIncFile->dwLine, pszValue);
            dwCnt++;
            TranslateOperator(pszValue);
            write(pIncFile, pszValue);
            pszValue = GetNextTokenPP(pIncFile);
        }
#if DYNPROTOQUALS
        if (g_bUseDefProto && *pszOut > '9') {
            if (strcmp(pszOut, "__declspec ( dllimport )") == 0) {
                convertline_register_qualifier(pIncFile, pszOut, FQ_IMPORT);
            } else {
                int nbStackItems = GetNumItemsLinkedList(ppszItems);
                for (int i = 0; i < nbStackItems; i++) {
                    char* item = (char*)GetItemLinkedList(ppszItems, i);
#ifdef _DEBUG
                    fprintf(stderr, "getting linkedlist item %X: %s\n", i, item);
#endif
                    struct LISTITEM* qualifierListItem = FindItemList(g_pQualifiers, item);
                    if (qualifierListItem != NULL) {
                        convertline_register_qualifier(pIncFile, item, qualifierListItem->value.u32);
                    }
                }
            }
        }
#endif
        if (!bExpression) {
            write(pIncFile, ">");
        }
    } else {
        write(pIncFile, "<>");
    }
    WriteComment(pIncFile);
    write(pIncFile, "\r\n");
    DestroyLinkedList(ppszItems);
}

void GetInterfaceName(char* pszName, char* pszInterface) {
    int nb = 0;
    while (1) {
        char c = *pszName++;
        if (c != '_') {
            nb++;
        } else if (nb != 0) {
            c = '\0';
        }
        *pszInterface++ = c;
        if (c == '\0') {
            break;
        }
    }
}

// check if macro is pattern "(this)->lpVtbl-><method>(this,...)"
// if yes, return name + method name

char* IsCObjMacro(struct INCFILE* pIncFile, char** pMethodName) {
    struct INPSTAT sis;
    char* tmpThisName;
    char* tmpMethodName;


    SaveInputStatus(pIncFile, &sis);
    char* token = GetNextTokenPP(pIncFile);
    if (token == NULL || strcmp(token, "(") != 0) {
        goto exit;
    }
    token = GetNextTokenPP(pIncFile); // Get name of THIS
    if (token == NULL) {
        goto exit;
    }
    tmpThisName = token;
    if (!IsName(pIncFile, token)) {
        goto exit;
    }
    token = GetNextTokenPP(pIncFile);
    if (token == NULL || strcmp(token, ")") != 0) {
        goto exit;
    }
    token = GetNextTokenPP(pIncFile);
    if (token == NULL || strcmp(token, "->") != 0) {
        goto exit;
    }
    token = GetNextTokenPP(pIncFile);
    if (token == NULL || strcmp(token, "lpVtbl") != 0) {
        goto exit;
    }
    token = GetNextTokenPP(pIncFile);
    if (token == NULL || strcmp(token, "->") != 0) {
        goto exit;
    }
    token = GetNextTokenPP(pIncFile); // get method name
    if (token == NULL) {
        goto exit;
    }
    tmpMethodName = token;
    token = GetNextTokenPP(pIncFile);
    if (token == NULL || strcmp(token, "(") != 0) {
        goto exit;
    }
    token = GetNextTokenPP(pIncFile);
    if (token == NULL) {
        goto exit;
    }
    if (strcmp(tmpThisName, token) != 0) { // is THIS?
        goto exit;
    }
    *pMethodName = tmpMethodName;
    return tmpThisName;
exit:
    RestoreInputStatus(pIncFile, &sis);
    return NULL;
}

void SkipPPLine(struct INCFILE* pIncFile) {
    while (1) {
        char* token = GetNextTokenPP(pIncFile);
        if (token != NULL) {
            break;
        }
    }
}

// size of output buffer pszNewType must be 256 bytes at least!

char* MakeType(char* pszType, int bUnsigned, int bLong, char* pszNewType) {
    if (pszType == NULL) {
        if (bLong) {
            pszType = "long";
            bLong = 0;
        } else {
            pszType = "int";
        }
    }
    if (bUnsigned) {
        sprintf(pszNewType, "unsigned %.246s", pszType);
    } else if (bLong) {
        sprintf(pszNewType, "long %.250s", pszType);
    } else {
        pszNewType = pszType;
    }
    return pszNewType;
}

// skip typecasts in preprocessor lines

void SkipCasts(struct INCFILE* pIncFile) {
    char* pszToken;
    int bIsName;
    int bUnsigned;
    int bLong;
    char* pszUnsigned;
    char* pszLong;
    char* pszPtr;
    struct INPSTAT sis;
    struct INPSTAT sis2;
    char szType[128];

    bIsName = 0;
    SaveInputStatus(pIncFile, &sis);
    while (1) {
        pszToken = GetNextTokenPP(pIncFile);
        if (pszToken == NULL) {
            break;
        }

        // skip MACRO(type) patterns

        if (IsName(pIncFile, pszToken)) {
            bIsName = 1;
            continue;
        }

        // check for '(' ... <*> ')' pattern

        if (*pszToken == '(' && !bIsName) {
            bUnsigned = 0;
            bLong = 0;
            SaveInputStatus(pIncFile, &sis2);
            char* token;
nexttoken:
            token = GetNextTokenPP(pIncFile);
            if (token != NULL) {
                if (strcmp(token, "unsigned") == 0) {
                    bUnsigned = 1;
                    pszUnsigned = token;
                    goto nexttoken;
                }
                if (strcmp(token, "long") == 0) {
                    bLong = 1;
                    pszLong = token;
                    goto nexttoken;
                }
            }
            if (token != NULL) {
                char* token2 = GetNextTokenPP(pIncFile);
                pszPtr = NULL;
                if (token2 != NULL && *token2 == '*') {
                    pszPtr = token2;
                    token2 = GetNextTokenPP(pIncFile);
                }
                if (token2 != NULL && *token2 == ')') {
                    char* type;
                    if (bUnsigned || bLong) {
                        type = TranslateType(MakeType(token, bUnsigned, bLong, szType), 0);
                    } else {
                        type = TranslateType(token, 0);
                    }
                    if (type != NULL && *type != '\0' && IsSimpleType(type)) {
                        *token = PP_IGNORE;
                        *token2 = PP_IGNORE;
                        if (pszPtr != NULL) {
                            *pszPtr = PP_IGNORE;
                        }
                        if (bUnsigned) {
                            *pszUnsigned = PP_IGNORE;
                        }
                        if (bLong) {
                            *pszLong = PP_IGNORE;
                        }
                        *pszToken = PP_IGNORE;
                    }
                }
            }
            RestoreInputStatus(pIncFile, &sis2);
        }
        bIsName = 0;
    }
    RestoreInputStatus(pIncFile, &sis);
}


// #define has occured
// can be a constant or a macro
// esi=input token stream

void IsDefine(struct INCFILE* pIncFile) {
    int bMacro;
    char szComment[2];
    char* pszName;
    char* pszValue;
    char* pszParm;
    char* pszToken;
    uint32_t dwParms;
    char* pszThis;
    int bIsCObj;
    char szInterface[128];
    char szMethod[128];

    char* storedPszOut = pIncFile->pszOut;
    pszName = GetNextTokenPP(pIncFile);  // get the name of constant/macro
    if (pszName != NULL) {
        szComment[0] = '\0'; szComment[1] = '\0';
        if (IsReservedWord(pszName)) {
            szComment[0] = ';';
            if (g_bWarningLevel > 0) {
                fprintf(stderr, "%s, %u: reserved word '%s' used as equate/macro\n", pIncFile->pszFileName, pIncFile->dwLine, pszName);
                pIncFile->dwWarnings++;
            }
        }
        SkipCasts(pIncFile);
        write(pIncFile, szComment);
        write(pIncFile, pszName);
        bMacro = pIncFile->pszIn[0] == (char)PP_MACRO && pIncFile->pszIn[1] == '\0';
        if (bMacro) {
            GetNextTokenPP(pIncFile);   // skip PP_MACRO
            GetNextTokenPP(pIncFile);   // skip "("
            write(pIncFile, " macro ");

            // write the macro params

#if 1
            SkipSimpleBraces(pIncFile);
#endif
            dwParms = 0;
            struct MACRO_TOKEN *params = NULL;
            struct MACRO_TOKEN *last_parm = NULL;
            while (1) {
                pszParm = GetNextTokenPP(pIncFile);
                if (pszParm == NULL) {
                    break;
                }
                if (*pszParm == ')') {
                    break;
                }
                if (*pszParm != ',') {
                    // Store name of parameter
                    struct MACRO_TOKEN *parm = malloc(sizeof(struct MACRO_TOKEN*));
                    parm->next = NULL;
                    parm->name = strdup(pszParm);
                    if (last_parm == NULL) {
                        params = parm;
                    } else {
                        last_parm->next = parm;
                    }
                    last_parm = parm;

                    dwParms++;
                    if (IsReservedWord(pszParm) && g_bWarningLevel > 1) {
                        fprintf(stderr, "%s, %u: reserved word '%s' used as macro parameter\n", pIncFile->pszFileName, pIncFile->dwLine, pszParm);
                        pIncFile->dwWarnings++;
                    }
                }
                write(pIncFile, pszParm);
            }
            write(pIncFile, "\r\n");
            write(pIncFile, szComment);
            struct ITEM_MACROINFO *macroInfo = NULL;


            // save macro name in symbol table
            if (IsMacro(pIncFile, pszName) == NULL) {
                macroInfo = InsertStrIntItem(pIncFile, g_pMacros, pszName, dwParms);
            }
            if (macroInfo) {
                macroInfo->params = malloc(dwParms * (sizeof(char*))+1);
                macroInfo->params[dwParms] = NULL;
                int i = 0;
                struct MACRO_TOKEN *p = params;
                for (; p != NULL; p = p->next, i++) {
                    macroInfo->params[i] = p->name;
                }
            }
            while (params != NULL) {
                struct MACRO_TOKEN *p = params->next;
                free(params);
                params = p;
            }
            write(pIncFile, "exitm <");

            int nbContents = 0;
            struct MACRO_TOKEN *contents = NULL;
            struct MACRO_TOKEN *last_contents = NULL;

            // test if it is a "COBJMACRO"

            char* pszMethod;
            pszThis = IsCObjMacro(pIncFile, &pszMethod);
            bIsCObj = pszThis != NULL;
            if (bIsCObj) {
                GetInterfaceName(pszName, szInterface);
                int bTrans;
                xprintf(pIncFile, "vf(%s, %s, %s)", pszThis, szInterface, TranslateName(pszMethod, NULL, &bTrans));
            }
            while (1) {
                char* token = GetNextTokenPP(pIncFile);
                if (token == NULL) {
                    break;
                }
                struct MACRO_TOKEN *content = malloc(sizeof(struct MACRO_TOKEN));
                content->name = strdup(token);
                content->next = NULL;
                if (last_contents == NULL) {
                    contents = content;
                } else {
                    last_contents->next = content;
                }
                nbContents++;
                last_contents = content;

                if (macroInfo != NULL && strcmp(token, "##") == 0) {
                    macroInfo->containsAppend = 1;
                }
                if (bIsCObj && strcmp(token, ")") == 0) {
                    continue;
                }
                write(pIncFile, TranslateOperator(token));
                write(pIncFile, " ");
            }

            if (macroInfo) {
                macroInfo->contents = malloc(nbContents * (sizeof(char*))+1);
                macroInfo->contents[nbContents] = NULL;
                int i = 0;
                struct MACRO_TOKEN *p = contents;
                for (; p != NULL; p = p->next, i++) {
                    macroInfo->contents[i] = p->name;
                }
            }
            while (contents != NULL) {
                struct MACRO_TOKEN *p = contents->next;
                free(contents);
                contents = p;
            }

            write(pIncFile, ">\r\n");
            write(pIncFile, szComment);
            write(pIncFile, "\tendm\r\n");
        } else {
            write(pIncFile, "\tEQU\t");
            SkipSimpleBraces(pIncFile);
            convertline(pIncFile, pszName);
        }
    }
    if (!g_bConstants) {
        pIncFile->pszOut = storedPszOut;
        *storedPszOut = '\0';
    }
}

void IsInclude(struct INCFILE* pIncFile) {
    char* pszPath;

    write(pIncFile, "\tinclude ");
    pszPath = GetNextTokenPP(pIncFile);
    if (pszPath != NULL && *pszPath == '<') {
        pszPath = GetNextTokenPP(pIncFile);
    }
    char *pszOut = pIncFile->pszOut;
    if (pszPath != NULL) {
        char sep = '\0';
        if (pszPath[0] == '"') {
            pszPath++;
            sep = '"';
        }
        while (*pszPath != '\0') {
            char c = *pszPath;
            pszPath++;
            if (c == sep) {
                break;
            }
            if (c == '\0') {
                break;
            }
            *pszOut = c;
            pszOut++;
        }
        char ext[2];
        memcpy(ext, &pszOut[-2], 2);
        if (strnicmp(ext, ".h", 2) == 0) {
            if (g_bProcessInclude) {
                ProcessFile(pIncFile->pszOut, pIncFile);
            }
            strcpy(&pszOut[-2], ".inc");
            pszOut += 2;
        }
        strcpy(pszOut, "\r\n");
        pszOut += 2;
    }
    pIncFile->pszOut = pszOut;
    *pszOut = '\0';
}

void IsError(struct INCFILE* pIncFile) {
    write(pIncFile, ".err <");
    for (int i = 0; 1; i++) {
        char* token = GetNextTokenPP(pIncFile);
        if (token == NULL) {
            break;
        }
        if (i > 0) {
            write(pIncFile, " ");
        }
        write(pIncFile, token);
    }
    write(pIncFile, " >\r\n");
}

void IsPragma(struct INCFILE* pIncFile) {
    struct INPSTAT sis;

    SaveInputStatus(pIncFile, &sis);
    char* token = GetNextTokenPP(pIncFile);
    if (strcmp(token, "message") != 0) {
        RestoreInputStatus(pIncFile, &sis);
        write(pIncFile, ";#pragma ");
        CopyLine(pIncFile);
        return;
    }
    token = GetNextTokenPP(pIncFile);  // skip '('
    if (token != NULL) {
        write(pIncFile, "%echo ");
        for (int i = 0; 1; i++) {
            token = GetNextTokenPP(pIncFile);
            if (token == NULL || *token == ')') {
                break;
            }
            if (i > 0) {
                write(pIncFile, " ");
            }
            if (*token == '*') {
                token++;
                token[strlen(token)-1] = '\0';
            }
            write(pIncFile, token);
            if (token != NULL) {
                SkipPPLine(pIncFile);
            }
            // check if last character is a ','
            // this causes %echo to continue with next line!
            if (pIncFile->pszOut[-1] == ',') {
                write(pIncFile, "'");
            }
            write(pIncFile, "\r\n");
        }
    }
}

char* TranslateIfExpression(char* pszToken) {
    for (size_t i = 0; i < sizeOpConvTab; i++) {
        if (strcmp(pszToken, g_szOpConvTab[i].wOp) == 0) {
            return g_szOpConvTab[i].pszSubst;
        }
    }
    return pszToken;
}

void IncIfLevel(struct INCFILE* pIncFile) {
    if (pIncFile->bIfLvl == MAXIFLEVEL) {
        fprintf(stderr, "%s, %u: if nesting level too deep\n", pIncFile->pszFileName, pIncFile->dwLine);
        pIncFile->dwErrors++;
    } else {
        pIncFile->bIfLvl++;
        pIncFile->bIfStack[pIncFile->bIfLvl] = 0;
    }
}

void IncElseLevel(struct INCFILE* pIncFile) {
    if (pIncFile->bIfLvl > 0) {
        pIncFile->bIfStack[pIncFile->bIfLvl]++;
    } else {
        fprintf(stderr, "%s, %u: else/elif withuot if\n", pIncFile->pszFileName, pIncFile->dwLine);
        pIncFile->dwErrors++;
    }
}

void DecIfLevel(struct INCFILE* pIncFile) {
    if (pIncFile->bIfLvl > 0) {
        pIncFile->bIfLvl--;
    } else {
        fprintf(stderr, "%s, %u: endif without if\n", pIncFile->pszFileName, pIncFile->dwLine);
        pIncFile->dwErrors++;
    }
}

// #if/#elif

static void IfElseIf_getifexpr(struct INCFILE* pIncFile, char* pszNot, int bMode) {
    if (!pIncFile->bDefinedMac) {
        pIncFile->bDefinedMac = 1;
        write(pIncFile, szMac_defined);
    }
    if (!bMode) {
        write(pIncFile, "if ");
    } else {
        write(pIncFile, "elseif ");
    }
    write(pIncFile, pszNot);
    write(pIncFile, "defined");
}

void IfElseIf(struct INCFILE* pIncFile, int bMode) {
    char* pszToken;
    char* pszNot;

    SkipCasts(pIncFile);
    pszToken = GetNextTokenPP(pIncFile);
    if (pszToken != NULL) {
        if (strcmp(pszToken, "!") == 0) {
            pszNot = "0 eq ";
            pszToken = GetNextToken(pIncFile);
        } else {
            pszNot = "";
        }
        if (strcmp(pszToken, "defined") == 0) {
            IfElseIf_getifexpr(pIncFile, pszNot, bMode);
            pszToken = GetNextTokenPP(pIncFile);
            goto exit;
        }
    }
    if (!bMode) {
        write(pIncFile, "if ");
    } else {
        write(pIncFile, "elseif ");
    }
exit:
    while (pszToken != NULL) {
        if (IsNumber(pszToken)) {
            write(pIncFile, pszToken);
        } else {
            write(pIncFile, TranslateIfExpression(pszToken));
        }
        pszToken = GetNextTokenPP(pIncFile);
    }
    write(pIncFile, "\r\n");
}

// #ifdef/#ifndef

void IfdefIfndef(struct INCFILE* pIncFile, char* pszCmd) {
    SkipCasts(pIncFile);
    char* pszToken = GetNextTokenPP(pIncFile);
    if (pszToken == NULL) {
        fprintf(stderr, "%s, %u: unexpected end of line\n", pIncFile->pszFileName, pIncFile->dwLine);
        pIncFile->dwErrors++;
        write(pIncFile, "if 0;");
    } else {
        if (IsReservedWord(pszToken)) {
            write(pIncFile, "if 0;");
        }
        write(pIncFile, pszCmd);
        write(pIncFile, " ");
        write(pIncFile, pszToken);
    }
    CopyLine(pIncFile);
}

// #else/#endif

void ElseEndif(struct INCFILE* pIncFile, char* pszCmd) {
    write(pIncFile, pszCmd);
    write(pIncFile, " ");
    CopyLine(pIncFile);
}

// -------------------

void IsIf(struct INCFILE* pIncFile) {
    IncIfLevel(pIncFile);
    IfElseIf(pIncFile, 0);
}

void IsElIf(struct INCFILE* pIncFile) {
    IncElseLevel(pIncFile);
    IfElseIf(pIncFile, 1);
}

void IsIfdef(struct INCFILE* pIncFile) {
    IncIfLevel(pIncFile);
    IfdefIfndef(pIncFile, "ifdef");
}

void IsIfndef(struct INCFILE* pIncFile) {
    IncIfLevel(pIncFile);
    IfdefIfndef(pIncFile, "ifndef");
}

void IsElse(struct INCFILE* pIncFile) {
    IncElseLevel(pIncFile);
    ElseEndif(pIncFile, "else");
}

void IsEndif(struct INCFILE* pIncFile) {
    DecIfLevel(pIncFile);
    ElseEndif(pIncFile, "endif");
}

void IsIfNP(struct INCFILE* pIncFile) {
    IncIfLevel(pIncFile);
    SkipPPLine(pIncFile);
}

void IsElIfNP(struct INCFILE* pIncFile) {
    IncElseLevel(pIncFile);
    SkipPPLine(pIncFile);
}

void IsElseNP(struct INCFILE* pIncFile) {
    IncElseLevel(pIncFile);
    SkipPPLine(pIncFile);
}

void IsEndifNP(struct INCFILE* pIncFile) {
    DecIfLevel(pIncFile);
    SkipPPLine(pIncFile);
}

// add preprocessor lines

void ParsePreProcs(struct INCFILE* pIncFile) {
    char* pszToken;

    pszToken = GetNextTokenPP(pIncFile);
    if (pszToken == NULL) {
        return;
    }
    debug_printf("%u: ParsePreProc, preproc command '%s' found\n", pIncFile->dwLine, pszToken);
    // SkipCasts(pIncFile);

    struct PPCMD *local_ppCmds;
    if (!pIncFile->bSkipPP) {
        local_ppCmds = ppcmds;
        if (!IsNewLine(pIncFile)) {
            write(pIncFile, "\r\n");
        }
    } else {
        local_ppCmds = ppcmdsnp;
    }
    while (local_ppCmds->pszCmd != NULL) {
        if (strcmp(pszToken, local_ppCmds->pszCmd) == 0) {
            local_ppCmds->pfnHandler(pIncFile);
            return;
        }
        local_ppCmds++;
    }
    if (pIncFile->bSkipPP) {
        SkipPPLine(pIncFile);
    } else {
        xprintf(pIncFile, ";#%s ", pszToken);
        CopyLine(pIncFile);
    }
}

char* GetNextToken(struct INCFILE* pIncFile) {
    if (pIncFile->bUseLastToken) {
        pIncFile->bUseLastToken = 0;
        pIncFile->bNewLine = 0;
        return pIncFile->pszLastToken;
    }
    do {
        size_t lenIn = strlen(pIncFile->pszIn);
        if (lenIn == 0) {
            pIncFile->bNewLine = 0;
            return NULL;
        }
        char* currentIn = pIncFile->pszIn;
        pIncFile->pszIn = currentIn + lenIn + 1;
        if (currentIn[0] == (char)PP_EOL && currentIn[1] == '\0') {
            pIncFile->dwLine++;
            pIncFile->bNewLine = 1;
            if (pIncFile->pszOut == pIncFile->pszOutStart || pIncFile->pszOut[-1] == '\n') {
                if (WriteComment(pIncFile)) {
                    write(pIncFile, "\r\n");
                }
            }
            continue;
        } else if (*currentIn == (char)PP_COMMENT) {
            if (!pIncFile->bSkipPP) {
                AddComment(currentIn);
            }
            continue;
        }
        if (pIncFile->bNewLine && strcmp(currentIn, "#") == 0) {
            if (WriteComment(pIncFile)) {
                write(pIncFile, "\r\n");
            }
            ParsePreProcs(pIncFile);
            continue;
        }
        pIncFile->bNewLine = 0;
        return currentIn;
    } while (1);
}

size_t PeekNextTokens(struct INCFILE* pIncFile, char** tokens, size_t count) {
    struct INPSTAT sis;
    char* pszToken;
    size_t res = 0;

    SaveInputStatus(pIncFile, &sis);
    pIncFile->bSkipPP++;
    for (size_t i = 0; i < count; i++) {
        tokens[i] = GetNextToken(pIncFile);
        if (!tokens[i]) {
            break;
        }
        res++;
    }
    pIncFile->bSkipPP--;
    RestoreInputStatus(pIncFile, &sis);
    return res;
}

char* PeekNextToken(struct INCFILE* pIncFile) {
    struct INPSTAT sis;
    char* pszToken;

    SaveInputStatus(pIncFile, &sis);
    pIncFile->bSkipPP++;
    pszToken = GetNextToken(pIncFile);
    pIncFile->bSkipPP--;
    RestoreInputStatus(pIncFile, &sis);
    return pszToken;
}

// check if token is a reserved name, if yes, add a '_' suffix
// return translated name in eax, edx=1 if translation occured

char* TranslateName(char* pszName, char* pszOut, int *bTranslateHappened) {
    if (IsReservedWord(pszName)) {
        if (pszOut == NULL) {
            pszOut = g_szTemp;
        }
        strcpy(pszOut, pszName);
        strcat(pszOut, "_");
        if (bTranslateHappened != NULL) {
            *bTranslateHappened = 1;
        }
        return pszOut;
    }
    if (bTranslateHappened != NULL) {
        *bTranslateHappened = 0;
    }
    return pszName;
}

void WriteExpression(struct INCFILE* pIncFile, struct LinkedList* pExpression) {
    uint32_t count = GetNumItemsLinkedList(pExpression);
    if (count > 0) {
        for (uint32_t i = 0; i < count; i++) {
            write(pIncFile, (char*)GetItemLinkedList(pExpression, i));
        }
    } else {
        write(pIncFile, "0");
    }
}

// pszName may be NULL

void AddMember(struct INCFILE* pIncFile, char* pszType, char* pszName, struct LinkedList* pszDup, int bIsStruct) {
    debug_printf("%u: AddMember %s %s\n", pIncFile->dwLine, pszType, pszName);
    if (pszName != NULL) {
        int bTranslated;
        pszName = TranslateName(pszName, NULL, &bTranslated);
        if (bTranslated && g_bWarningLevel > 1) {
            fprintf(stderr, "%s, %u: reserved word '%s' used as struct/union member\n", pIncFile->pszFileName, pIncFile->dwLine, pszName);
            pIncFile->dwWarnings++;
        }
        write(pIncFile, pszName);
    }
    write(pIncFile, "\t");
    pszType = TranslateType(pszType, g_bUntypedMembers);
    write(pIncFile, pszType);
    if (pszDup != NULL) {
        write(pIncFile, " ");
        WriteExpression(pIncFile, pszDup);
        DestroyLinkedList(pszDup);
        write(pIncFile, " dup (");
    } else {
        write(pIncFile, "\t");
    }
    if (bIsStruct || IsStructure(pszType)) {
        write(pIncFile, "<>");
    } else {
        write(pIncFile, "?");
    }
    if (pszDup != NULL) {
        write(pIncFile, ")");
    }
    WriteComment(pIncFile);
    write(pIncFile, "\r\n");
}

// preserve all registers
// return C if current if level is NOT active

int IsIfLevelActive(struct INCFILE* pIncFile, struct INPSTAT *pStat) {
    return pIncFile->bIfLvl == pStat->bIfLvl && pIncFile->bIfStack[pIncFile->bIfLvl] != pStat->bIfStack[pStat->bIfLvl];
}

// find name of a struct/union, if any
// use pszStructName if name is a macro
// out: eax = struct name
//	  edx = flags (if name is a macro)

char* GetStructName(struct INCFILE* pIncFile, char* pszStructName, uint32_t* dwFlagsOut) {
    uint32_t dwCntBrace;
    char* pszName;
    uint32_t dwFlags;
    struct INPSTAT sis;

    debug_printf("%u: GetStructName enter\n", pIncFile->dwLine);
    pszName = NULL;
    dwFlags = 0;
    SaveInputStatus(pIncFile, &sis);
    pIncFile->bSkipPP++;
    dwCntBrace = 1;
    char* token;
    while (dwCntBrace != 0) {
        token = GetNextToken(pIncFile);
        if (token == NULL) {
            break;
        }
        if (IsIfLevelActive(pIncFile, &sis)) {
            continue;
        }
        if (strcmp(token, "{") == 0) {
            dwCntBrace++;
        } else if (strcmp(token, "}") == 0) {
            dwCntBrace--;
        }
    }
    if (token != NULL) {
        token = GetNextToken(pIncFile);
        if (token != NULL && *token != ';') {
            if (*token == '*') {
                goto exit;
            }
            pszName = token;

            // there may come a type qualifier or a '*'
            // in which case there is no name

            char* typeQual = ConvertTypeQualifier(token);
            if (typeQual == NULL) {
                pszName = NULL;
                goto exit;
            }

            struct ITEM_MACROINFO* macroLI = IsMacro(pIncFile, pszName);
            if (dwFlags != 0) {
                char* curPszOut = pIncFile->pszOut;
                pIncFile->pszOut = pszStructName;
                MacroInvocation(pIncFile, pszName, macroLI, 0);
                pIncFile->pszOut = curPszOut;
                pszName = pszStructName;
            }
            debug_printf("%u: GetStructName, end of struct %s found\n", pIncFile->dwLine, pszName);
        }
    }
    if (pszName == NULL) {
        debug_printf("%u: GetStructName, no name found\n", pIncFile->dwLine);
    }
exit:
    debug_printf("%u: GetStructName exit\n", pIncFile->dwLine);
    pIncFile->bSkipPP--;
    RestoreInputStatus(pIncFile, &sis);
    if (dwFlagsOut != NULL) {
        *dwFlagsOut = dwFlags;
    }
    return pszName;
}

int HasVTable(struct INCFILE* pIncFile) {
    int dwRC;
    uint32_t dwCntBrace;
    struct INPSTAT sis;

    debug_printf("%u: HasVTable enter\n", pIncFile->dwLine);
    SaveInputStatus(pIncFile, &sis);
    pIncFile->bSkipPP++;
    dwCntBrace = 1;
    dwRC = 0;
    while (dwCntBrace > 0) {
        char* token = GetNextToken(pIncFile);
        if (token == NULL) {
            break;
        }
        if (IsIfLevelActive(pIncFile, &sis)) {
            continue;
        }
        if (strcmp(token, "{") == 0) {
            dwCntBrace++;
        } else if (strcmp(token, "}") == 0) {
            dwCntBrace--;
        } else if (dwCntBrace == 1 && strcmp(token, "virtual") == 0) {
            dwRC = 1;
            break;
        }
    }
    pIncFile->bSkipPP--;
    RestoreInputStatus(pIncFile, &sis);
    return dwRC;
}

// determine if it is a "function" or "function ptr" declaration

int IsFunctionPtr(struct INCFILE* pIncFile) {
    uint32_t dwCntBrace;
    int bRC;
    struct INPSTAT sis;

    SaveInputStatus(pIncFile, &sis);
    pIncFile->bSkipPP++;
    dwCntBrace = 1;
    bRC = 0;
    char* token;
    while (dwCntBrace != 0) {
        token = GetNextToken(pIncFile);
        if (token == NULL) {
            break;
        }
        if (IsIfLevelActive(pIncFile, &sis)) {
            continue;
        }
        if (strcmp(token, "(") == 0) {
            dwCntBrace++;
        } else if (strcmp(token, ")") == 0) {
            dwCntBrace--;
        }
    }
    if (token != NULL) {
        token = GetNextToken(pIncFile);
        if (token != NULL && *token == '(') {
            bRC = 1;
        }
    } else {
        debug_printf("%s, %u: unexpected eof\n", pIncFile->pszFileName, pIncFile->dwLine);
    }
exit:
    pIncFile->bSkipPP--;
    RestoreInputStatus(pIncFile, &sis);
    return bRC;
}

// determine if current item is a "function" declaration
// required if keyword "struct" has been found in input stream
// may be a struct declaration or a function returning a struct (ptr)
// workaround:
// + if "*" is found before next ";" or ",", it is a function <= disabled this one, needs counter-example
// + if "(" is found before next ";" or ",", it is a function
// + if "{" is found before next ";" or ",", it is a structure

int IsFunction(struct INCFILE* pIncFile) {
    int bRC;
    struct INPSTAT sis;

    SaveInputStatus(pIncFile, &sis);
    pIncFile->bSkipPP++;
    bRC = 0;
    while (1) {
        char* token = GetNextToken(pIncFile);
        if (token == NULL) {
            break;
        }
        if (IsIfLevelActive(pIncFile, &sis)) {
            continue;
        }
        if (*token == ';' || *token == ',' || *token == '{') {
            break;
        }
        if (/**token == '*' ||*/ *token == '(') {
            token = GetNextToken(pIncFile);
            if (token != NULL && *token == '*') {
                bRC = 0;
            } else {
                bRC = 1;
            }
            break;
        }
    }
    pIncFile->bSkipPP--;
    RestoreInputStatus(pIncFile, &sis);
    return bRC;
}

int IsRecordEnd(struct INCFILE* pIncFile) {
    int bRC;
    struct INPSTAT sis;

    SaveInputStatus(pIncFile, &sis);
    pIncFile->bSkipPP++;
    bRC = 1;
    while (1) {
        char* token = GetNextToken(pIncFile);
        if (token == NULL) {
            break;
        }
        if (IsIfLevelActive(pIncFile, &sis)) {
            continue;
        }
        if (*token == ';' || *token == ',') {
            break;
        }
        if (*token == ':') {
            bRC = 1;
            break;
        }
    }
    pIncFile->bSkipPP--;
    RestoreInputStatus(pIncFile, &sis);
    return bRC;
}

// skip name of a struct/union

void SkipName(struct INCFILE* pIncFile, char* pszName, uint32_t dwNameFlags) {
    if (pszName == NULL) {
        return;
    }
    GetNextToken(pIncFile); // skip name
    if (dwNameFlags == 0) {
        return;
    }
    char* token = PeekNextToken(pIncFile);
    if (token == NULL || strcmp(token, "(") != 0) {
        return;
    }
    while (1) {
        token = GetNextToken(pIncFile);
        if (token == NULL) {
            break;
        }
        if (strcmp(token, ")") == 0) {
            break;
        }
    }
}

int IsPublicPrivateProtected(char* pszToken) {
    return strcmp(pszToken, "private") == 0 || strcmp(pszToken, "public") == 0 || strcmp(pszToken, "protected") == 0;
}

// returns eax == 1 if union/struct/class
// edx == 1 if class

int IsUnionStructClass(char* pszToken, int* outIsClass) {
    if (strcmp(pszToken, "union") == 0 || strcmp(pszToken, "struct") == 0) {
        *outIsClass = 0;
        return 1;
    }
    if (strcmp(pszToken, "class") == 0) {
        *outIsClass = 1;
        return 1;
    }
    *outIsClass = 0;
    return 0;
}

// get a variable declaration
// <type> name<[]> , followed by ";" or "," or "{"
// type:
// <unsigned> <stdcall?> typename <<far|near> *>
// a C++ syntax may be found as well:
// <type> name(...)< ; | {...}>

// bMode:
//  DT_STANDARD = standard (union|struct)
//  DT_EXTERN = extern
//  DT_ENUM = enum
// out: eax=next token


char* GetDeclaration(struct INCFILE* pIncFile, char* pszToken, char* pszParent, int bMode) {
    int bBits;
    int bPtr;
    int bFunction;
    int bIsVirtual;
    int bUnsigned;
    int bLong;
    int bStatic;
    uint8_t bStruct;
    uint32_t dwCnt;
    uint32_t dwRes;
    uint32_t dwPtr;
    uint32_t dwBits;
    uint32_t dwNameFlags;
    char* pszType;
    char* pszName;
    struct LinkedList* pszDup;
    char* pszRecordType;
    char* pszBits;
    char* pszEndToken;
    char* dwEsp;
    char szStructName[MAXSTRUCTNAME];
    char szRecord[64];
    char szType[256];
    char szTmp[8];
    char szName[128];

    if (pszParent == NULL) {
        pszParent = "";
    }
    dwEsp = pIncFile->pszStructName;
    pIncFile->pszStructName = pszParent;

    bBits = 0;
    pszType = NULL;
    dwRes = 0;
    bUnsigned = 0;
    bLong = 0;
    bStruct = 0;
    bStatic = 0;
    dwBits = 0;
nextscan:
    bFunction = 0;
    bIsVirtual = 0;
    pszBits = NULL;
    dwPtr = 0;
    pszName = NULL;
    pszDup = NULL;

    while (1) {
        if (pszToken == NULL || *pszToken == ';' || *pszToken == '}' || *pszToken == ',') {
            break;
        }

        if (strcmp(pszToken, "union") == 0) {
            if (bMode == DT_EXTERN) {
                pszToken = GetNextToken(pIncFile);
                continue;
            }
            // union
            debug_printf("%u, GetDeclaration: %s.union found\n", pIncFile->dwLine, pszParent);
            write(pIncFile, "union");
            pszToken = GetNextToken(pIncFile);
            if (pszToken == NULL) {
                goto error;
            }
            char* nextToken;
            if (IsName(pIncFile, pszToken)) {
                nextToken = GetNextToken(pIncFile);
                if (nextToken == NULL) {
                    goto error;
                }
            } else {
                nextToken = pszToken;
            }
            if (strcmp(nextToken, "{") == 0) {
                pszName = GetStructName(pIncFile, szStructName, &dwNameFlags);
                if (pszName != NULL) {
                    write(pIncFile, " ");
                    write(pIncFile, TranslateName(pszName, NULL, NULL));
                }
                write(pIncFile, "\r\n");
                if (!getblock(pIncFile, pszName, DT_STANDARD, pszParent)) {
                    goto done;
                }
                SkipName(pIncFile, pszName, dwNameFlags);
            } else {
                fprintf(stderr, "%s, %u: union without block\n", pIncFile->pszFileName, pIncFile->dwLine);
                pIncFile->dwErrors++;
                write(pIncFile, "\r\n");
            }
            pszType = NULL;
            pszName = NULL;
            write(pIncFile, "ends\r\n");
            debug_printf("%u: end of union\n", pIncFile->dwLine);
            goto nextitem;
        }

        if (strcmp(pszToken, "struct") == 0) {
            if (bMode == DT_EXTERN) {
                pszToken = GetNextToken(pIncFile);
                continue;
            }
            // struct
            debug_printf("%u, GetDeclaration: %s.struct found\n", pIncFile->dwLine, pszParent);
            char* nextToken = GetNextToken(pIncFile);
            if (nextToken == NULL || strcmp(nextToken, ";") == 0) {
                goto error;
            }
            debug_printf("%u, GetDeclaration: %s.struct, next token %s\n", pIncFile->dwLine, pszParent, nextToken);
            if (strcmp(nextToken, "{") != 0) {
                pszName = nextToken;
                nextToken = PeekNextToken(pIncFile);
                if (nextToken != NULL && strcmp(nextToken, "{") == 0) {
                    nextToken = GetNextToken(pIncFile);
                } else {
                    nextToken = pszName;
                    pszName = NULL;
                }
            }
            if (strcmp(nextToken, "{") == 0) {
                write(pIncFile, "struct");
                char* structName = GetStructName(pIncFile, szStructName, &dwNameFlags);
                if (structName != NULL) {
                    pszName = structName;
                }
                if (structName != NULL) {
                    write(pIncFile, " ");
                    write(pIncFile, TranslateName(pszName, NULL, NULL));
                }
                write(pIncFile, "\r\n");
                if (!getblock(pIncFile, pszName, DT_STANDARD, pszParent)) {
                    goto done;
                }
                SkipName(pIncFile, pszName, dwNameFlags);
                write(pIncFile, "ends\r\n");
                debug_printf("%u: end of struct\n", pIncFile->dwLine);
            } else if (strcmp(nextToken, "*") == 0) {
                dwPtr++;
            } else {
                // found "struct tagname
                pszType = nextToken;
                pszName = NULL;
                bPtr = 0;
                if (IsName(pIncFile, nextToken)) {
                    while (1) {
                        char* token = GetNextToken(pIncFile);
                        if (token == NULL) {
                            goto error;
                        }
                        if (*token == ';' || *token == ',') {
                            break;
                        }
                        if (strcmp(token, "*") == 0) {
                            bPtr++;
                        } else {
                            token = ConvertTypeQualifier(token);
                            if (*token == '\0') {
                                continue;
                            }
                            if (IsName(pIncFile, token)) {
                                pszName = token;
                            }
                        }
                    }
                    if (pszName != NULL) {
                        if (pszType != NULL && !bPtr) {
                            xprintf(pIncFile, "%s\t%s<>\r\n", pszName, pszType);
                        } else {
                            xprintf(pIncFile, "%s\t%s\t?\r\n", pszName, g_b64bit ? "QWORD" : "DWORD");
                        }
                    } else {
                        bStruct = 1;
                        goto nextitem;
                    }
                } else {
                    fprintf(stderr, "%s, %u: unexpected item %s after 'struct'\n", pIncFile->pszFileName, pIncFile->dwLine, pszType);
                    pIncFile->dwErrors++;
                }
            }
            pszType = NULL;
            pszName = NULL;
            goto nextitem;
        }

        // end union + struct

        if (strcmp(pszToken, "unsigned") == 0) {
            bUnsigned = 1;
            goto nextitem;
        }
        if (strcmp(pszToken, "signed") == 0) {
            bUnsigned = 0;
            goto nextitem;
        }
        if (strcmp(pszToken, "long") == 0) {
            bLong = 1;
            goto nextitem;
        }
        if (strcmp(pszToken, "static") == 0) {
            bStatic = 1;
            goto nextitem;
        }

        // check for pattern "public:","private:","protected:"
        if (IsPublicPrivateProtected(pszToken)) {
            char* peek = PeekNextToken(pIncFile);
            if (peek != NULL && strcmp(peek, ":") == 0) {
                xprintf(pIncFile, ";%s:\r\n", pszToken);
                goto nextitem;
            }
        }

        if (strcmp(pszToken, "operator") == 0) {
            // operator
            fprintf(stderr, "%s, %u: C++ syntac ('operator') found\n", pIncFile->pszFileName, pIncFile->dwLine);
            while (1) {
                pszToken = GetNextToken(pIncFile);
                if (pszToken == NULL || strcmp(pszToken, ";") == 0) {
                    break;
                }
                if (strcmp(pszToken, "{") == 0) {
                    uint32_t braceCnt = 1;
                    while (braceCnt > 0) {
                        char* token = GetNextToken(pIncFile);
                        if (strcmp(token, "{") == 0) {
                            braceCnt++;
                        } else if (strcmp(token, "}") == 0) {
                            braceCnt--;
                        }
                    }
                    break;
                }
            }
            pszType = NULL;
            pszName = NULL;
            pIncFile->dwErrors++;
            break;
        }

        // friend
        if (strcmp(pszToken, "friend") == 0) {
            goto nextitem;
        }

        if (strcmp(pszToken, "virtual") == 0) {
            bIsVirtual = 1;
            goto nextitem;
        }

        struct ITEM_MACROINFO* macroInfo = IsMacro(pIncFile, pszToken);
        if (macroInfo != NULL) {
            int res;
            if (bMode == DT_ENUM) {
                res = MacroInvocation(pIncFile, pszToken, macroInfo, 0);
                pszName = "";
            } else {
                res = MacroInvocation(pIncFile, pszToken, macroInfo, 1);
            }
            if (res) {
                goto nextitem;
            }
        }

        if (strcmp(pszToken, "=") == 0) {
            if (bMode == DT_ENUM) {
                goto nextitem;
            } else if (bMode == DT_EXTERN) {
                char* token;
                while (1) {
                    token = GetNextToken(pIncFile);
                    if (token == NULL || strcmp(token, ";") == 0 || strcmp(token, ",") == 0) {
                        break;
                    }
                }
                pszToken = token;
                continue;
            }
        }

        if (strcmp(pszToken, ":") == 0) {
            char* token = GetNextToken(pIncFile);
            if (token == NULL) {
                goto error;
            }
            pszBits = token;
            goto nextitem;
        }

        if (strcmp(pszToken, "[") == 0) {
            pszDup = CreateLinkedList();
            while (1) {
                char* token = GetNextToken(pIncFile);
                if (token == NULL || strcmp(token, ";") == 0) {
                    goto error;
                }
                if (strcmp(token, "]") == 0) {
                    break;
                }
                AddLinkedList(pszDup, (uintptr_t)token);
            }
            goto nextitem;
        }   // '['

        if (strcmp(pszToken, "~") == 0 && pIncFile->bIsClass) {
            char* token = GetNextToken(pIncFile);
            if (token == NULL) {
                goto error;
            }
            *szName = '`';
            strcpy(&szName[1], token);
            pszToken = szName;
        }

        if (strcmp(pszToken, "(") == 0 && bMode != DT_ENUM) {
            bFunction = 1;
            if (IsFunctionPtr(pIncFile)) {
                debug_printf("%u: GetDeclaration, function ptr found\n", pIncFile->dwLine);
                char* name;
                if (ParseTypedefFunctionPtr(pIncFile, pszParent, &name) == 0) {
                    // clear dereference count (was part of function return type)
                    dwPtr = 0;
                    pszName = name;
                    if (pIncFile->bIsInterface) {
                        pszName = NULL;
                        pszType = NULL;
                    } else if (g_bUntypedMembers) {
                        pszType = "DWORD";
                    } else {
                        sprintf(szType, "p%s_%s", pszParent, pszName);
                        pszType = szType;
                    }
                    bFunction = 0;
                } else {
                    goto error;
                }
            } else {
                if (pszName != NULL) {
                    debug_printf("%u: GetDeclaration, function %s found\n", pIncFile->dwLine, pszName);
                    ParseTypedefFunction(pIncFile, pszName, 1, pszParent);
                    if (bMode == DT_STANDARD) {
                        pszName = NULL;
                        pszType = NULL;
                    }
                    if (bIsVirtual) {
                        char* peekToken = PeekNextToken(pIncFile);
                        if (peekToken != NULL && *peekToken == '=') {
                            GetNextToken(pIncFile);
                            GetNextToken(pIncFile);
                        }
                    }
                } else {
                    goto error;
                }
            }
            goto nextitem;
        }

        if (strcmp(pszToken, "*") == 0 || strcmp(pszToken, "&") == 0) {
            dwPtr++;
            goto nextitem;
        }

        if (bMode != DT_ENUM) {
            char* typedefQ = ConvertTypeQualifier(pszToken);
            if (*typedefQ == '\0') {
                goto nextitem;
            }
            pszToken = typedefQ;
        } else {
            if (pszType == NULL) {
                pszType = TranslateName(pszToken, NULL, NULL);
                write(pIncFile, pszType);
                write(pIncFile, " = ");
            } else {
                pszName = pszToken;
                write(pIncFile, TranslateOperator(pszName));
                if (*pszToken >= '0') {
                    pIncFile->dwEnumValue = atol(pszToken) + 1;
                }
                write(pIncFile, " ");
            }
            goto nextitem;
        }

        if (IsName(pIncFile, pszToken)) {
            if (pszName != NULL) {
                pszType = pszName;
            }
            pszName = pszToken;
        } else {
            goto error;
        }
nextitem:
        pszToken = GetNextToken(pIncFile);
    }
    pszEndToken = pszToken;

    if (bMode == DT_EXTERN || bStatic) {
        if (pszName != NULL) {
            char* transName = TranslateName(pszName, NULL, NULL);
            if (pIncFile->pszPrefix != NULL) {
                write(pIncFile, pIncFile->pszPrefix);
                pIncFile->pszPrefix = NULL;
            }
            if (bStatic) {
                xprintf(pIncFile, ";externdef syscall ?%s@%s@@___", transName, pszParent);
            } else {
                write(pIncFile, transName);
            }
            write(pIncFile, ": ");
            while (dwPtr > 0) {
                write(pIncFile, "ptr ");
                dwPtr--;
            }
            if (bFunction) {
                write(pIncFile, "near");
            } else {
                pszType = MakeType(pszType, bUnsigned, bLong, szType);
                // fprintf(stderr, "GetDeclaration extern: type = %s\r\n", pszType);
                write(pIncFile, TranslateType(pszType, g_bUntypedMembers));
            }
        }
        write(pIncFile, "\r\n");
    } else if (bMode == DT_ENUM) {
        if (pszType != NULL && pszName == NULL) {
            sprintf(szTmp, "%u", pIncFile->dwEnumValue);
            write(pIncFile, szTmp);
            pIncFile->dwEnumValue++;
        }
        write(pIncFile,"\r\n");
    } else {
        // bitfield start
        if (pszBits != 0) {
            // it is possible that NO name is supplied for a record field!
            if (pszName == NULL) {
                sprintf(szName, "res%u", dwRes);
                dwRes++;
                pszName = szName;
            }
            if (!bBits) {
                sprintf(szRecord, "%s_R%u", pszParent, pIncFile->dwRecordNum);
                pIncFile->dwRecordNum++;
                if (g_bNoRecords) {
                    write(pIncFile, "szRecord");
                    write(pIncFile, "\tRECORD\t");
                }
                bBits = 1;
                pszType = MakeType(pszType, bUnsigned, bLong, szType);
                pszRecordType = TranslateType(pszType, 0);
                debug_printf("%u: new Bitfield: %s\n", pIncFile->dwLine, pszRecordType);
            }
            char* transName = TranslateName(pszName, NULL, NULL);
            if (!g_bNoRecords) {
                write(pIncFile, transName);
                write(pIncFile, ": ");
                write(pIncFile, pszBits);
                debug_printf("%u: new Bits: %s\n", pIncFile->dwLine, pszName);
            } else {
                int nbBits = atol(pszBits);
                int mask = ((1 << (nbBits + dwBits)) - 1)  & ~((1 << dwBits) - 1);
                dwBits = dwBits + nbBits;
                xprintf(pIncFile, "%s_%s equ 0x%xh\r\n", szRecord, pszName, mask);
            }
            if (IsRecordEnd(pIncFile)) {
                if (!g_bNoRecords) {
                    write(pIncFile, "\r\n");
                }
                if (g_bRecordsInUnions) {
                    write(pIncFile, "union\r\n\t");
                    write(pIncFile, pszRecordType);
                    write(pIncFile, "\t?\r\n");
                } else if (g_bNoRecords) {
                    xprintf(pIncFile, "%s\t%s\t?\r\n", szRecord, pszRecordType);
                }
                if (!g_bNoRecords) {
                    write(pIncFile, "\t");
                    write(pIncFile, szRecord);
                    write(pIncFile, " <>\r\n");
                }
                if (g_bRecordsInUnions) {
                    write(pIncFile, "ends\r\n");
                }
            } else {
                if (!g_bNoRecords) {
                    write(pIncFile, ",");
                }
                pszToken = GetNextToken(pIncFile);
                if (pszEndToken != NULL && *pszEndToken == ';') {
                    pszType = NULL;
                }
                goto nextscan;
            }
            // bitfield end
        } else if (pszName != NULL) {
            if (dwPtr != 0) {
                pszType = g_b64bit ? "QWORD" : "DWORD";
            } else {
                if (pszType == NULL) {
                    if (IsStructure(pszName)) {
                        pszType = pszName;
                        pszName = NULL;
                    }
                }
                pszType = MakeType(pszType, bUnsigned, bLong, szType);
            }
            AddMember(pIncFile, pszType, pszName, pszDup, bStruct);
            if (pszToken != NULL && *pszToken == ',') {
                pszToken = GetNextToken(pIncFile);
                goto nextscan;
            }
        }
    }
done:
    return pszToken;
error:
    pIncFile->pszStructName = dwEsp;
    fprintf(stderr, "%s, %u: unexpected item %s.%s\n", pIncFile->pszFileName, pIncFile->dwLine, pszParent, pszToken);
    pIncFile->dwErrors++;
    return pszToken;
}

// get members of a block (structure, union, enum)
// esi: input tokens
// dwMode:
//  DT_STANDARD: variable declaration in struct/union
//  DT_EXTERN: extern declaration
//  DT_ENUM: enum declaration
// out: eax == 1: ok
//      eax == 0: skip further block processing

int getblock(struct INCFILE* pIncFile, char* pszStructName, uint32_t dwMode, char* pszParent) {
    uint8_t ifLvl;
    uint8_t bIf;
    int dwRes;

    ifLvl = pIncFile->bIfLvl;
    bIf = pIncFile->bIfStack[ifLvl];

    if (pszStructName != NULL) {
        pIncFile->dwRecordNum = 0;
    }

    for (int i = 1; i != 0; ) {
        char* token = GetNextToken(pIncFile);
        if (token == NULL) {
            break;
        }
        uint8_t lIfLvl = pIncFile->bIfLvl;
        uint8_t lBif = pIncFile->bIfStack[lIfLvl];
        if (lIfLvl == ifLvl && bIf != lBif) {
            pIncFile->pszLastToken = token;
            pIncFile->bUseLastToken = 1;
            dwRes = 0;
            goto exit;
        }
        if (strcmp(token, "{") == 0) {
            i++;
            continue;
        } else if (strcmp(token, "}") == 0) {
            i--;
            continue;
        }
        char* decl;
        if (pszStructName != NULL) {
            decl = GetDeclaration(pIncFile, token, pszStructName, dwMode);
        } else {
            decl = GetDeclaration(pIncFile, token, pszParent, dwMode);
        }
        if (decl == NULL) {
            break;
        }
        if (strcmp(decl, "}") == 0) {
            i--;
        }
    }
    dwRes = 1;
exit:
    debug_printf("%u: getblock %s, end of struct found [%u]\n", pIncFile->dwLine, pszStructName, dwRes);
    return dwRes;
}

// check out if there come some
// pointer definitions behind

int GetFurtherTypes(struct INCFILE* pIncFile, char* pszType, char* pszTag, char* pszToken) {
    uint8_t bPtr;
    char* pszName;

    bPtr = 0;
    pszName = NULL;
    while (1) {
        if (*pszToken == ',' || *pszToken == ';') {
            if (pszName != NULL) {
                char* pTypeStr = pszType;
                if (pTypeStr == NULL) {
                    pTypeStr = pszTag;
                }
                debug_printf("%u: GetFurtherTypes '%s %s'\n", pIncFile->dwLine, pszName, pTypeStr);
                // Coment out forward declarations
                if (!bPtr) {
                    if (strcmp(pszName, pTypeStr) == 0) {
                        write(pIncFile, ";");
                    }
                }
                write(pIncFile, pszName);
                write(pIncFile, " typedef ");
                while (bPtr) {
                    write(pIncFile, "ptr ");
                    bPtr--;
                }
                write(pIncFile, pTypeStr);
                write(pIncFile, "\r\n");
                pszToken = pTypeStr;
            }
            if (*pszToken == ';') {
                break;
            }
            pszName = NULL;
            bPtr = 0;
        } else if (*pszToken == '*') {
            bPtr++;
        } else {
            char* convQual = ConvertTypeQualifier(pszToken);
            if (*convQual != '\0') {
                pszName = convQual;
            }
        }
nextitem:
        pszToken = GetNextToken(pIncFile);
        if (pszToken == NULL) {
            return 12;
        }
    }
    if (pszName != NULL) {
        write(pIncFile, "\r\n");
    }
    return 0;
}

int HasVirtualBase(struct LinkedList* pszInherit) {
    uint32_t nb = GetNumItemsLinkedList(pszInherit);

    for (uint32_t i = 0; i < nb; i++) {
        if (strcmp((char*)GetItemLinkedList(pszInherit, i), "virtual") == 0) {
            return 1;
        }
    }
    return 0;
}

void WriteInherit(struct INCFILE* pIncFile, struct LinkedList* pszInherit, int bPreClass) {
    int bVirtual;
    int bVbtable;
    char* pszToken;

    uint32_t nbItems = GetNumItemsLinkedList(pszInherit);
    bVirtual = 0;
    bVbtable = 0;
    for (uint32_t i = 0; i < nbItems; i++) {
        pszToken = (char*)GetItemLinkedList(pszInherit, i);
        if (strcmp(pszToken, "virtual") == 0) {
            if (bPreClass && !bVbtable) {
                write(pIncFile, "DWORD ?\t;vbtable\r\n");
                bVbtable = 1;
            }
            bVirtual = 1;
        } else {
            if (IsPublicPrivateProtected(pszToken)) {
                if (bPreClass) {
                    xprintf(pIncFile, ";%s:\r\n", pszToken);
                }
            } else {
                if (bVirtual) {
                    if (!bPreClass) {
                        xprintf(pIncFile, "\t%s <>\r\n", pszToken);
                    }
                } else if (bPreClass) {
                    xprintf(pIncFile, "\t%s <>\r\n", pszToken);
                }
                bVirtual = 0;
            }
        }
    }
}

// class tname{};
// typedef struct/union tname name;
// typedef struct/union <tname> {} name;
// typedef struct/union <tname> * name; does not fully work!!!

int ParseTypedefUnionStruct(struct INCFILE* pIncFile, char* pszToken, int bIsClass) {
    int dwRes;
    char* pszStruct;
    char* pszName;
    char* pszType;
    char* pszTag;
    struct LinkedList* pszInherit;
    char* pszSuffix;
    char* pszAlignment;
    int bSkipName;
    int bHasVTable;
    uint8_t bPtr;
    char szType[256];
    char szStructName[MAXSTRUCTNAME];
    char szNoName[64];

    pszStruct = pszToken;
    pszTag = NULL;
    pszType = NULL;
    pszName = NULL;
    pszInherit = NULL;
    bPtr = 0;
    if (bIsClass) {
        pszStruct = "struct";
        pIncFile->bIsClass = 1;
    }
    debug_printf("%u: ParseTypedefUnionStruct '%s'\n", pIncFile->dwLine, pszStruct);
    char* token = GetNextToken(pIncFile);
    if (token != NULL && *token != '{') {
        debug_printf("%u: ParseTypedefUnionStruct, token '%s' assumed tag\n", pIncFile->dwLine, token);
        pszTag = token;
        pszType = token;
        token = GetNextToken(pIncFile);
    }
    if (token == NULL) {
        dwRes = 2;
        goto error;
    }
    if (*token == ':') {
        if (0) { // (!bIsClass)
            fprintf(stderr, "%s, %u: C++ syntax found\n", pIncFile->pszFileName, pIncFile->dwLine);
        }
        while (1) {
            token = GetNextToken(pIncFile);
            if (token == NULL || *token == ';') {
                dwRes = 9;
                goto error;
            }
            if (strcmp(token, "{") == 0) {
                break;
            }
            if (strcmp(token, ",") == 0) {
                continue;
            }
            pszToken = token;
            if (pszInherit == NULL) {
                pszInherit = CreateLinkedList();
            }
            AddLinkedList(pszInherit, (uintptr_t)pszToken);
        }
    }
    debug_printf("%u: ParseTypedefUnionStruct, token '%s' found\n", pIncFile->dwLine, token);
    if (strcmp(token, "{") == 0) {
        if (g_bAddAlign && !pIncFile->bAlignMac) {
            write(pIncFile, szMac_align);
            pIncFile->bAlignMac = 1;
        }
        bHasVTable = 0;
        if (bIsClass) {
            bHasVTable = HasVTable(pIncFile);
        }
        char* structName = GetStructName(pIncFile, szStructName, NULL);
        bSkipName = 1;
        if (structName == NULL) {
            structName = pszTag;
            bSkipName = 0;
        }
        // no name at all?
        if (structName == NULL) {
            sprintf(szNoName, "__H2INCC_STRUCT_%04u", g_dwStructSuffix);
            g_dwStructSuffix++;
            structName = szNoName;
        }

        pszType = TranslateName(structName, szType, NULL);
        InsertItem(pIncFile, g_pStructures, pszType);
        if (pszTag != NULL && strcmp(pszTag, pszType) != 0) {
            InsertStrStrItem(pIncFile, g_pStructureTags, pszTag, pszType);
        }
        pszSuffix = "";
        if (pszInherit) {
            if (HasVirtualBase(pszInherit)) {
                // pszSuffix = "$";
            }
        }
        char* alignment = GetAlignment(pszType);
        if (alignment != NULL) {
            pszAlignment = alignment;
        } else if (g_bAddAlign) {
            pszAlignment = "@align";
        } else {
            pszAlignment = "";
        }
        xprintf(pIncFile, "%s%s\t%s %s\r\n", pszType, pszSuffix, pszStruct, pszAlignment);
        if (bHasVTable) {
            xprintf(pIncFile, "\t%s ?\t;`vftable'\r\n", g_b64bit ? "QWORD" : "DWORD");
        }
        if (pszInherit != NULL) {
            WriteInherit(pIncFile, pszInherit, 1);
        }
        int block;
        if (bIsClass) {
            block = getblock(pIncFile, pszType, DT_STANDARD, pszTag);
        } else {
            block = getblock(pIncFile, pszType, DT_STANDARD, NULL);
        }
        if (block == 0) {
            goto done;
        }
        if (pszInherit) {
            if (HasVirtualBase(pszInherit)) {
                // xprintf(pIncFile, "%s\t%s %s\r\n", pszType, pszStruct, pszAlignment);
                // xprintf(pIncFile, "\t%s%s <>\r\n", pszType, pszSuffix);
                WriteInherit(pIncFile, pszInherit, 0);
                // xprintf(pIncFile, "%s\tends\r\n", pszType);
            }
        }
        xprintf(pIncFile, "%s%s\tends\r\n", pszType, pszSuffix);
        if (bSkipName) {
            GetNextToken(pIncFile); // skip structure name
        }
        token = GetNextToken(pIncFile);

        // typedef struct/union tagname typename
    }
    if (token != NULL) {
        GetFurtherTypes(pIncFile, pszType, pszTag, token);
    }
done:
    return 0;
error:
    pIncFile->bIsClass = 0;
    return dwRes;
}

// <typedef> <qualifiers> enum <tname> {x<=a>,y<=b>,...} name<,*name>;
// simplest form is "enum {x = a, y = b};"

int ParseTypedefEnum(struct INCFILE* pIncFile, int bIsTypedef) {
    int dwRes;
    char* pszTag;
    char* pszName;
    char szStructName[MAXSTRUCTNAME];

    pszTag = NULL;
    pszName = NULL;
    char* token = GetNextToken(pIncFile);
    if (token != NULL && *token != '{') {
        pszTag = token;
        token = GetNextToken(pIncFile);
    }
    if (token != NULL && *token == '{') {
        char* name;
        if (bIsTypedef) {
            name = GetStructName(pIncFile, szStructName, NULL);
        } else {
            name = NULL;
        }
        if (name == NULL) {
            name = pszTag;
        }
        if (name != NULL) {
            pszName = name;
            xprintf(pIncFile, "%s typedef DWORD\r\n", name);
        }
        pIncFile->dwEnumValue = 0;
        getblock(pIncFile, pszName, DT_ENUM, NULL);
        write(pIncFile, "\r\n");
        if (bIsTypedef) {
            GetNextToken(pIncFile); // skip enum name;
        }
        token = GetNextToken(pIncFile);
        if (token != NULL && pszName != NULL) {
            dwRes = GetFurtherTypes(pIncFile, pszName, NULL, token);
        }
    } else {
        // just syntax "typedef enum oldtypename newtypename;"
        if (IsName(pIncFile, token)) {
            write(pIncFile, token);
            write(pIncFile, " typedef DWORD\r\n");
            dwRes = 0;
        } else {
            dwRes = 2;
        }
    }
    return dwRes;
}

char* GetCallConvention(uint32_t dwQualifiers) {
    dwQualifiers &= FQ_STDCALL | FQ_CDECL | FQ_PASCAL | FQ_SYSCALL;
    if (dwQualifiers == 0) {
        dwQualifiers = g_dwDefCallConv;
    }
    if (dwQualifiers & FQ_STDCALL) {
        return "stdcall";
    } else if (dwQualifiers & FQ_CDECL) {
        return "c";
    } else if (dwQualifiers & FQ_PASCAL) {
        return "pascal";
    } else if (dwQualifiers & FQ_SYSCALL) {
        return "syscall";
    } else {
        return "";
    }
}

// return NULL if no proto qualifier
struct ITEM_STRINT* CheckProtoQualifier(struct INCFILE* pIncFile, char* pszToken) {
    struct INPSTAT sis;
    int bIsDeclSpec;
    struct ITEM_STRINT* res;

    bIsDeclSpec = 0;
    if (strcmp(pszToken, "__declspec") == 0) {
        SaveInputStatus(pIncFile, &sis);
        char* token = GetNextToken(pIncFile);
        if (token != NULL && *token == '{') {
            token = GetNextToken(pIncFile);
            if (token != NULL) {
                pszToken = token;
                bIsDeclSpec = 1;
            }
        } else {
            RestoreInputStatus(pIncFile, &sis);
        }
    }
#if 1
#if DYNPROTOQUALS
    res = FindItemList(g_pQualifiers, pszToken);
#else
    res = list_bsearch(pszToken, g_ProtoQualifiers.pItems, g_protoqualifiers.numItems, 2*sizeof(char*), cmpproc);
#endif
#else
    struct ITEM_STRSTR* pos = g_ProtoQualifiers;
    while (1) {
        if (pos == NULL) {
            break;
        }
        if (strcmp(pos->key, pszToken) == 0) {
            res = pos;
            break;
        }
        pos++;
    }
#endif
done:
    if (bIsDeclSpec) {
        GetNextToken(pIncFile);
    }
    return res;
}

//typedef function pointer
//typedef <qualifiers><returntype> ( <qualifiers> * <name> )(<parameters>)
//return: 0 ok (+name), else eax=error code
//the first "(" has been read already!
//this function has to be reentrant, since a function parameter
//may have type "function ptr"

int ParseTypedefFunctionPtr(struct INCFILE* pIncFile, char* pszParent, char** outPszName) {
    char* pszToken;
    char* pszName;
    char* pszType;
    uint32_t dwCnt;
    uint32_t dwQualifier;
    uint8_t bPtr;
    char szPrototype[768];
    char szType[128];
    int dwRes;

    bPtr = 0;
    szPrototype[0] = '\0';
    pszName = NULL;
    dwQualifier = 0;

    while (1) {
        pszToken = GetNextToken(pIncFile);
        if (pszToken == NULL) {
            dwRes = 15;
            goto error;
        }
        if (*pszToken == ')' || *pszToken == ':') {
            break;
        }
        if (*pszToken == '*') {
            bPtr++;
        } else {
            struct ITEM_STRINT* qualItem = CheckProtoQualifier(pIncFile, pszToken);
            if (qualItem != NULL) {
                dwQualifier |= qualItem->value;
            } else {
                pszName = pszToken;  // ignore any qualifiers
            }
        }
    }
    if (*pszToken == ')') {
        debug_printf("%u: ParseTypedefFunctionPtr %s\n", pIncFile->dwLine, pszName);
        char* token = GetNextToken(pIncFile);
        if (token == NULL) {
            dwRes = 16;
            goto error;
        }
        // get function parameters
        if (*token == '(') {
            char* prototypedefPos = szPrototype;
            if (pszName != NULL) {
                char* callConv = GetCallConvention(dwQualifier);
                if (pIncFile->bIsInterface && (dwQualifier & FQ_STDCALL)) {
                    sprintf(szPrototype, "STDMETHOD %s, ", TranslateName(pszName, NULL, NULL));
                } else if (pszParent != NULL) {
                    sprintf(szPrototype, "proto%s_%s typedef proto %s ", pszParent, pszName, callConv);
                } else {
                    sprintf(szPrototype, "proto_%s typedef proto %s ", pszName, callConv);
                }
                prototypedefPos += strlen(szPrototype);
            }
            pszType = NULL;
            bPtr = 0;
            dwCnt = 0;
            while (1) {
                pszToken = GetNextToken(pIncFile);
                if (pszToken == NULL || *pszToken == ';') {
                    dwRes = 11;
                    goto error;
                }
                if (*pszToken == ',' || *pszToken == ')') {
                    if (pszType != 0) {
                        debug_printf("%u: ParseTypedefFunctionPtr, parameter %s found\n", pIncFile->dwLine, pszType);
                        pszType = TranslateType(pszType, g_bUntypedParams);
                        if (*pszType == '\0') {
                            pszType = NULL;
                        }
                    }
                    if (bPtr != 0 || pszType != NULL) {
                        strcat(prototypedefPos, ":");
                        while (bPtr > 0) {
                            strcat(prototypedefPos, "ptr ");
                            bPtr--;
                        }
                        if (pszType != NULL) {
                            strcat(prototypedefPos, pszType);
                        }
                    }
                    if (dwCnt == 0 && pIncFile->bIsInterface) {
                        *prototypedefPos = '\0';
                    }
                    dwCnt++;
                    if (*pszToken == ')') {
                        break;
                    }
                    strcat(szPrototype, ",");
                    if (dwCnt == 1 && pIncFile->bIsInterface) {
                        *prototypedefPos = '\0';
                    }
                    pszType = NULL;
                    bPtr = 0;
                    continue;
                }
                if (*pszToken == '*') {
                    bPtr++;
                    continue;
                } else if (*pszToken == '[') {
                    bPtr++;
                    while (1) {
                        char* token = GetNextToken(pIncFile);
                        if (token == NULL || strcmp(token, "]") == 0 || strcmp(token, ";") == 0) {
                            break;
                        }
                    }
                    continue;
                } else if (*pszToken == '(') {
                    // function ptr as function parameter?
                    if (IsFunctionPtr(pIncFile)) {
                        uint8_t originalInterface = pIncFile->bIsInterface;
                        pIncFile->bIsInterface = 0;
                        char* name;
                        ParseTypedefFunctionPtr(pIncFile, pszName, &name);
                        pIncFile->bIsInterface = originalInterface;
                        sprintf(szType, "p%s_%s", pszName, name);
                        pszType = szType;
                    }
                    continue;
                }
                if (strcmp(pszToken, "struct") == 0) {
                    continue;
                }
                char* typeQual = ConvertTypeQualifier(pszToken);
                if (*typeQual == '\0') {
                    continue;
                }
                if (pszType == NULL || strcmp(pszType, "const") == 0) {
                    pszType = pszToken;
                }
            }
            if (pszName != NULL) {
                write(pIncFile, szPrototype);
                write(pIncFile, "\r\n");
                if (pIncFile->bIsInterface) {
                } else {
                    if (pszParent != NULL) {
                        xprintf(pIncFile, "p%s_%s", pszParent, pszName);
                    } else {
                        xprintf(pIncFile, "%s", TranslateName(pszName, NULL, NULL));
                    }
                    write(pIncFile, " typedef ");
                    if (pszParent != NULL) {
                        xprintf(pIncFile, "ptr proto%s_%s\r\n", pszParent, pszName);
                    } else {
                        xprintf(pIncFile, "ptr proto_%s\r\n", pszName);
                    }
                }
            }
#if 0
            // may be an inline function!
            if (bAcceptBody) {
                char* tok = PeekNextToken(pIncFile);
                if (tok != NULL && strcmp(tok, "{") == 0) {
                    tok = GetNextToken(pIncFile);
                    dwCnt = 1;
                    while (dwCnt != 0) {
                        tok = GetNextToken(pIncFile);
                        if (tok == NULL) {
                            break;
                        }
                        if ((trcmp(tok, "{")) {
                            dwCnt++;
                        } else if (strcmp(tok, "}") == 0) {
                            dwCnt--;
                        }
                    }
                }
            }
#endif
        } else {
            dwRes = 17;
            debug_printf("%u: ParseTypeDefFunctionPtr error %u\n", pIncFile->dwLine, dwRes);
            goto error;
        }
    } else {
        dwRes = 18;
        debug_printf("%u: ParseTypeDefFunctionPtr error %u\n", pIncFile->dwLine, dwRes);
        goto error;
    }
    if (outPszName != NULL) {
        *outPszName = pszName;
    }
    return 0;
error:
    return dwRes;
}

// + typedef <qualifiers> returntype name(<parameters>)<{...}>;
// or in a class definition:
// + <qualifiers> returntype name(<parameters>)<{...}>;

int ParseTypedefFunction(struct INCFILE* pIncFile, char* pszName, int bAcceptBody, char* pszParent) {
    char* pszToken;
    char* pszType;
    uint32_t dwCnt;
    uint32_t dwNum;
    uint32_t cCallConv;
    uint8_t bPtr;
    int bFirstParam;
    int dwRes;
    char* pszDecoName;
    char szFuncName[32];

    bFirstParam = 0;
    if (pIncFile->bIsClass) {
        pszDecoName = malloc((2 * strlen(pszParent) + 64) & ~3);
        dwNum = 0;
        char* name = pszName;
        if (*name == '~') {
            name++;
            dwNum++;
        }
        if (strcmp(name, pszParent) == 0) {
            sprintf(szFuncName, "?%u", dwNum);
            pszName = szFuncName;
        }
        cCallConv = 'A';  // A=cdecl, G=stdcall
        sprintf(pszDecoName, "?%s@%s@@Q%c___Z", pszName, pszParent, cCallConv);
        xprintf(pIncFile, ";externdef syscall %s:near\r\n", pszDecoName);
        xprintf(pIncFile, ";%s proto :ptr %s", pszDecoName, pszParent);
        bFirstParam = 1;
    } else {
        xprintf(pIncFile, "%s typedef proto stdcall ", pszName);
    }
    pszType = NULL;
    bPtr = 0;
    while (1) {
        char* token = GetNextToken(pIncFile);
        if (token == NULL) {
            dwRes = 11;
            goto error;
        }
        if (*token == ',' || *token == ')') {
            if (bPtr || pszType != NULL) {
                if (pszType != NULL) {
                        pszType = TranslateType(pszType, g_bUntypedParams);
                }
                if (bPtr || *pszType != '\0') {
                    if (bFirstParam) {
                        write(pIncFile, ",");
                        bFirstParam = 0;
                    }
                    write(pIncFile, ":");
                }
                while (bPtr > 0) {
                    write(pIncFile, "ptr ");
                    bPtr--;
                }
                if (pszType != NULL) {
                    write(pIncFile, pszType);
                }
            }
            if (*token == ')') {
                break;
            }
            write(pIncFile, ",");
            pszType = NULL;
            bPtr = 0;
            continue;
        }
        if (*token == '*') {
            bPtr++;
            continue;
        }
        pszToken = token;
        if (strcmp(pszToken, "struct") == 0) {
            continue;
        }
        char* typeQual = ConvertTypeQualifier(pszToken);
        if (*typeQual == '\0') {
            continue;
        }
        if (pszType == NULL) {
            pszType = pszToken;
        }
    }
    write(pIncFile, "\r\n");
    if (bAcceptBody) {
        char* tok = PeekNextToken(pIncFile);
        if (tok != NULL && strcmp(tok, "{") == 0) {
            tok = GetNextToken(pIncFile);
            dwCnt = 1;
            while (dwCnt != 0) {
                tok = GetNextToken(pIncFile);
                if (tok == NULL) {
                    break;
                }
                if (strcmp(tok, "{")) {
                    dwCnt++;
                } else if (strcmp(tok, "}") == 0) {
                    dwCnt--;
                }
            }
        }
    }
    return 0;
error:
    return dwRes;
}

// typedef occured
// syntax:
// + typedef <qualifiers> type <<far|near> *> newname<[]>;
// + typedef struct/union <tname> {} name;
// + typedef struct/union <tname> * name; does not work!!!
// + typedef <qualifiers> enum <tname> {x<=a>,y<=b>,...} name;
// + typedef <qualifiers> returntype (<qualifiers> *name)(<parameters>);
// + typedef <qualifiers> returntype name(<parameters>);
// esi -> tokens behind "typedef"

int ParseTypedef(struct INCFILE* pIncFile) {
    char* pszName;
    char* pszToken;
    char* pszType;
    char* pszDup;
    uint32_t dwCntBrace;
    uint32_t dwSquareBraces;
    uint8_t bPtr;
    int bValid;
    int bUnsigned;
    struct INPSTAT sis;
    char szTmpType[64];
    char szType[256];
    char* token;
    int dwRC;

    debug_printf("%u: ParseTypedef begin\n", pIncFile->dwLine);
    bUnsigned = 0;
nexttoken:
    pszToken = GetNextToken(pIncFile);
    char *nextTokens[3];
    int countNextTokens = PeekNextTokens(pIncFile, nextTokens, sizeof(nextTokens) / sizeof(*nextTokens));
    if (pszToken != NULL) {
        pszToken = TranslateToken(pszToken);
    }
    if (pszToken == NULL || *pszToken == ';') {
        dwRC = 1;
        goto error;
    }
    if (*pszToken == '[') {
        debug_printf("%u: ParseTypedef: '[' found\n", pIncFile->dwLine);
        SaveInputStatus(pIncFile, &sis);
        char* token = GetNextToken(pIncFile);
        if (token != NULL) {
            if (strcmp(token, "pubilc") == 0) {
                token = GetNextToken(pIncFile);
                if (token != NULL && *token == ']') {
                    debug_printf("%u: Parsetypedef: ']' found\n", pIncFile->dwLine);
                    goto nexttoken;
                }
            }
        }
        RestoreInputStatus(pIncFile, &sis);
    }

    if (strcmp(pszToken, "const") == 0) {
        goto nexttoken;
    }

    // syntax: "typedef <macro()> xxx"
    struct ITEM_MACROINFO* macroInfo = IsMacro(pIncFile, pszToken);
    if (macroInfo != NULL) {
        debug_printf("%u: ParseTypedef, macro invocation %s\n", pIncFile->dwLine, pszToken);
        if (MacroInvocation(pIncFile, pszToken, macroInfo, 1)) {
            goto nexttoken;
        }
    }

    // syntax: "typedef union|struct"?
    int isClass;
    if (IsUnionStructClass(pszToken, &isClass) && contains("{", nextTokens, countNextTokens)) {
        debug_printf("%u: ParseTypedef, '%s' found\n", pIncFile->dwLine, pszToken);
        dwRC = ParseTypedefUnionStruct(pIncFile, pszToken, isClass);
        goto exit;
    }

    // syntax: "enum"?
    if (strcmp(pszToken, "enum") == 0) {
        debug_printf("%u: ParseTypedef, 'enum' found\n", pIncFile->dwLine);
        dwRC = ParseTypedefEnum(pIncFile, 1);
        goto exit;
    }

    // pszToken may be OLD TYPE
    if (strcmp(pszToken, "unsigned") == 0) {
        bUnsigned = 1;
        goto nexttoken;
    }
    if (strcmp(pszToken, "signed") == 0) {
        bUnsigned = 0;
        goto nexttoken;
    }

    if (bUnsigned) {
        pszToken = MakeType(pszToken, bUnsigned, 0, szType);
    }

    pszType = TranslateType(pszToken, 0);
    bPtr = 0;
    pszName = NULL;
    pszDup = NULL;
    dwSquareBraces = 0;
    while (1) {
        token = GetNextToken(pIncFile);
        if (token == NULL) {
            dwRC = 2;
            goto error;
        }
        if (*token == ',' || *token == ';') {
            if (pszName != NULL) {
                bValid = 1;
                // don't add "<newname> typedef <oldname>" entrief if <newname> == <oldname>
                if (!bPtr) {
                    if (strcmp(pszName, pszType) == 0) {
                        write(pIncFile, ";");
                        bValid = 0;
                    }
                }
                if (bValid) {
                    int transHappened;
                    char *transName = TranslateName(pszName, szType, &transHappened);
                    if (transHappened && g_bWarningLevel > 0) {
                        fprintf(stderr, "%s, %u: reserved word '%s' used as typedef\n", pIncFile->pszFileName, pIncFile->dwLine, pszName);
                        pIncFile->dwWarnings++;
                    }
                    pszName = transName;
                }
                debug_printf("%u: new typedef %s =%s\n", pIncFile->dwLine, pszName, pszType);
                // if there is an array index, create a struct instead of a typedef!
                if (pszDup && bPtr == 0) {
                    write(pIncFile, pszName);
                    write(pIncFile, " struct\r\n");
                    write(pIncFile, "\t");
                    write(pIncFile, pszType);
                    write(pIncFile, " ");
                    write(pIncFile, pszDup);
                    write(pIncFile, " dup (?)\r\n");
                    write(pIncFile, pszName);
                    write(pIncFile, " ends\r\n");
                } else {
                    write(pIncFile, pszName);
                    write(pIncFile, " typedef ");
                    for (uint32_t i = 0; i < bPtr; i++) {
                        write(pIncFile, "ptr ");
                    }
                    write(pIncFile, pszType);
                    write(pIncFile, "\r\n");
                }
                // add type to structure table if necessary
                if (bValid && !bPtr) {
                    if (IsStructure(pszType)) {
                        InsertItem(pIncFile, g_pStructures, pszName);
                    }
                }
#if TYPEDEFSUMMARY
                if (g_bTypedefSummary && bValid) {
                    InsertItem(pIncFile, g_pTypedefs, pszName);
                }
#endif
            }
            bPtr = 0;
            pszName = NULL;
            if (*token == ';') {
                break;
            }
        }
        if (*token == '(') {
            if (IsFunctionPtr(pIncFile)) {
                dwRC = ParseTypedefFunctionPtr(pIncFile, NULL, NULL);
            } else {
                token = (pszName == NULL) ? pszType : pszName;
                dwRC = ParseTypedefFunction(pIncFile, token, 0, NULL);
            }
            pszName = NULL;
            if (dwRC != 0) {
                goto exit;
            }
        } else if (*token == '*' && dwSquareBraces == 0) {
            bPtr++;
        } else if (*token == '[') {
            debug_printf("%u: ParseTypedef, '[' found\n", pIncFile->dwLine);
            dwSquareBraces++;
        } else if (*token == ']') {
            debug_printf("%u: ParseTypedef, ']' found\n", pIncFile->dwLine);
            dwSquareBraces--;
        } else {
            if (dwSquareBraces) {
                if (pszDup != NULL) {
                    char* txt = malloc(strlen(token) + strlen(pszDup) + 2);
                    strcpy(txt, pszDup);
                    free(pszDup);
                    pszDup = txt;
                    strcat(pszDup, " ");
                    strcat(pszDup, token);
                } else {
                    pszDup = token;
                }
                debug_printf("%u: ParseTypedef, array size '%s' found\n", pIncFile->dwLine, token);
                continue;
            }
            token = ConvertTypeQualifier(token);
            if (*token == '\0') {
                continue;
            }
            pszToken = token;
            if (IsName(pIncFile, token)) {
                pszName = pszToken;
            }
        }
    }
    dwRC = 0;
exit:
error:
    if (dwRC != 0) {
        fprintf(stderr, "%s, %u: unexpected item %s in typedef [%p]\n", pIncFile->pszFileName, pIncFile->dwLine, pszToken, token);
        pIncFile->dwErrors++;
    }
    debug_printf("%u: ParseTypedef end\n", pIncFile->dwLine);
    return dwRC;
}

#if 0
int CheckApiType(char* pszToken) {
    struct ITEM_STRSTR* pos = g_pFirstApi;
    while (pos != NULL) {
        if (strcmp(pszToken, pos->value)) {
            return 1;
        }
    }
    return 0;
}
#endif

void ParseExtern(struct INCFILE* pIncFile) {
    char* pszToken;

    while (1) {
        pszToken = GetNextToken(pIncFile);
        if (pszToken == NULL) {
            break;
        }
        if (*pszToken == ';') {
            break;
        }
        if (strcmp(pszToken, "\"C\"") == 0) {
            write(pIncFile, ";extern \"C\"\r\n");
            pIncFile->bC = 1;
            break;
        } else if (strcmp(pszToken, "\"C++\"") == 0) {
            write(pIncFile, ";extern \"C++\"\r\n");
            break;
        }
        pIncFile->pszPrefix = "externdef ";
        if (pIncFile->bC) {
            write(pIncFile, "c ");
        }
        GetDeclaration(pIncFile, pszToken, NULL, DT_EXTERN);
        break;
    }
}

char* TranslateName2(struct INCFILE* pIncFile, char* pszFuncName) {
    int transHappened;
    char* transName = TranslateName(pszFuncName, NULL, &transHappened);
    if (transHappened && g_bWarningLevel > 0) {
        fprintf(stderr, "%s, %u: reserved word '%s' used as prototype\n", pIncFile->pszFileName, pIncFile->dwLine, pszFuncName);
    }
    pIncFile->dwWarnings++;
    return transName;
}

// parse a function prototype
void ParsePrototype(struct INCFILE* pIncFile, char* pszFuncName, char* pszImpSpec, char* szCallConv) {
    int bUnsigned;
    uint32_t dwPtr;
    uint32_t dwCnt;
    uint32_t dwParmBytes;
    int bFunctionPtr;
    char* pszType;
    char* pszName;
    char* pszToken;
    char* pszPrefix;
    struct INPSTAT sis;
    char szSuffix[8];
    char szType[512];

    debug_printf("%u: ParsePrototype name=%s, pszImpSpec=%p\n", pIncFile->dwLine, pszFuncName, pszImpSpec);
    if (g_bUseDefProto && pszImpSpec) {
    } else {
        if (g_bAssumeDllImport) {
            pIncFile->dwQualifiers |= FQ_IMPORT;
        } else if (g_bIgnoreDllImport) {
            pIncFile->dwQualifiers &= ~FQ_IMPORT;
        }
    }
    char* pszCallConv = GetCallConvention(pIncFile->dwQualifiers);
    if (g_bUseDefProto && pszImpSpec != NULL) {
        char* suffix;
        if (IsReservedWord(pszFuncName)) {
            fprintf(stderr, "%s, %u: reserved word '%s' used as prototype\n", pIncFile->pszFileName, pIncFile->dwLine, pszFuncName);
            pIncFile->dwWarnings++;
            suffix = "_";
        } else {
            suffix = "";
        }
        xprintf(pIncFile, "@DefProto %s, %s, %s, %s, <", pszImpSpec, pszFuncName, pszCallConv, suffix);
    } else if (pIncFile->dwQualifiers & FQ_IMPORT) {
        xprintf(pIncFile, "proto_%s typedef proto %s ", pszFuncName, pszCallConv);
    } else {
        xprintf(pIncFile, "%s proto %s", TranslateName2(pIncFile, pszFuncName), pszCallConv);
    }
    pIncFile->pszLastToken = NULL;
    pszType = NULL;
    pszName = NULL;
    bFunctionPtr = 0;
    dwPtr = 0;
    dwParmBytes = 0;
    dwCnt = 1;
    bUnsigned = 0;
    while (dwCnt != 0) {
        char* token = GetNextToken(pIncFile);
        if (token == NULL) {
            break;
        }
        if (*token == ';') {
            break;
        }
        if (*token == ',' || *token == ')') {
            if (pszName != NULL ||dwPtr || bUnsigned) {
                if (pszType == NULL) {
                    pszType = pszName;
                    pszName = NULL;
                }
                char* typeStr = pszType;
                if (dwPtr != 0 && typeStr == NULL) {
                    typeStr = "";  // make sure it's a valid LPSTR
                } else {
                    pszType = MakeType(pszType, bUnsigned, 0, szType);
                    typeStr = TranslateType(pszType, g_bUntypedParams);
                    pszType = typeStr;
                }
                // don't interpret xxx(void) as parameter
                if (dwPtr == 0 && *typeStr == '\0') {
                } else {
                    if (dwParmBytes) {
                        write(pIncFile, " :");
                    } else {
                        write(pIncFile, ":");
                    }
                    if (dwPtr != 0) {
                        dwParmBytes = 4;
                    } else {
                        dwParmBytes = GetTypeSize(pszType);
                    }
                }
                while (dwPtr != 0) {
                    write(pIncFile, "ptr ");
                    dwPtr--;
                }
                if (pszType != NULL) {
                    write(pIncFile, pszType);
                }
                pszType = NULL;
                pszName = NULL;
                bFunctionPtr = 0;
                bUnsigned = 0;
            }
            if (*token == ')') {
                dwCnt--;
            } else {
                char *nextToken = PeekNextToken(pIncFile);
                if (strcmp(nextToken, "...") != 0) {
                    write(pIncFile, token);
                }
            }
        } else if (*token == '*' || *token == '&') {
            dwPtr++;
        } else if (strcmp(token, "[") == 0) {
            dwPtr++;
            while (1) {
                token = GetNextToken(pIncFile);
                if (token == NULL || strcmp(token, "]") == 0 || strcmp(token, ";") == 0) {
                    break;
                }
            }
        } else if (*token == '(') {
            // function ptr as function parameter?
            if (IsFunctionPtr(pIncFile)) {
                char* pszOutBackup = pIncFile->pszOut;
                ParseTypedefFunctionPtr(pIncFile, NULL, NULL);
                pIncFile->pszOut = pszOutBackup;
                // sprintf(szType, "p%s_%s", pszName, typedefOut);
                dwPtr = 1;
                pszName = NULL;
            } else {
                dwCnt++;
            }
        } else {
            pszToken = token;
            char* typeQual = ConvertTypeQualifier(pszToken);
            if (*typeQual == '\0') {
                continue;
            }
            if (strcmp(pszToken, "struct") == 0) {
                continue;
            }
            if (strcmp(pszToken, "unsigned") == 0) {
                bUnsigned = 1;
                continue;
            }
            if (strcmp(pszToken, "...") == 0) {
                continue;
            }
            pszType = pszName;
            pszName = pszToken;
        }
    }
    if (g_bUseDefProto && pszImpSpec) {
        write(pIncFile, ">");
        pIncFile->dwQualifiers &= ~FQ_IMPORT;
        if (pIncFile->dwQualifiers & FQ_STDCALL) {
            xprintf(pIncFile, ", %u", dwParmBytes);
        }
    }
    write(pIncFile, "\r\n");
    if (pIncFile->dwQualifiers & FQ_IMPORT) {
#if 1
        if (pIncFile->dwQualifiers & FQ_STDCALL) {
            pszPrefix = "_";
            sprintf(szSuffix, "@%u", dwParmBytes);
        } else if (pIncFile->dwQualifiers & FQ_CDECL) {
            pszPrefix = "_";
            *szSuffix = '\0';
        } else {
            pszPrefix = "";
            *szSuffix = '\0';
        }
#endif
        xprintf(pIncFile, "externdef strcall _imp_%s%s%s: ptr proto_%s\r\n", pszPrefix, pszFuncName, szSuffix, pszFuncName);
        xprintf(pIncFile, "%s equ <_imp_%s%s%s>\r\n", TranslateName2(pIncFile, pszFuncName), pszPrefix, pszFuncName, szSuffix);
    }
#if PROTOSUMMARY
    if (g_bProtoSummary) {
        InsertItem(pIncFile, g_pPrototypes, pszFuncName);
    }
#endif
    if (g_bCreateDefs) {
        InsertDefItem(pIncFile, pszFuncName, dwParmBytes);
    }
    if (pIncFile->dwQualifiers & FQ_INLINE) {
        SaveInputStatus(pIncFile, &sis);
        char* token = GetNextToken(pIncFile);
        if (token != NULL && *token == '{') {
            dwCnt = 1;
            while (dwCnt != 0) {
                token = GetNextToken(pIncFile);
                if (token == NULL) {
                    break;
                }
                if (*token == '{') {
                    dwCnt++;
                } else if (*token == '}') {
                    dwCnt--;
                }
            }
        } else {
            RestoreInputStatus(pIncFile, &sis);
        }
    }
}

static int FindInArray(const char *needle, const char **array) {
    for (int i = 0; array[i] != NULL; i++) {
        if (strcmp(needle, array[i]) == 0) {
            return i;
        }
    }
    return -1;
}

// a known macro has been found
// if bit 0 of pNameItem is set then it is a macro from h2incc.ini
// returns 1 if macro was invoked
// else 0 (turned out to be NO macro invocation)

int MacroInvocation(struct INCFILE* pIncFile, char* pszToken, struct ITEM_MACROINFO* pMacroInfo, int bWriteLF) {
    uint32_t dwCnt;
    uint32_t dwParms;   // parameters for macro
    uint32_t dwFlags;   // flags from h2incc.ini
    char* pszType;
    char* pszName;
    uint8_t bPtr;
    char* pszOutSave;
    int dwRC;

    if (pMacroInfo->flags & 1) {
        dwParms = 0;
        dwFlags = (uintptr_t)pMacroInfo;
    } else {
        dwParms = pMacroInfo->flags; // number of parameters
        dwFlags = 0;
    }
    pszOutSave = pIncFile->pszOut;

    if (pMacroInfo->containsAppend) {
        int nbParams = 0;
        struct MACRO_TOKEN *param_tokens = NULL;
        struct MACRO_TOKEN *last_param_token = NULL;
        char *token = PeekNextToken(pIncFile);
        if (token != NULL && *token == '(') {
            token = GetNextToken(pIncFile);
            dwCnt = 1;
            while (1) {
                token = GetNextToken(pIncFile);
                if (token == NULL) {
                    break;
                }
                if (*token == ')') {
                    dwCnt--;
                } else if (*token == '(') {
                    dwCnt++;
                }
                if (dwCnt == 0) {
                    break;
                }
//                if (dwFlags & MF_PARAMS) {
//                    token = TranslateName(token, NULL, NULL);
//                }
                debug_printf("%u: macro parameter: %s\n", pIncFile->dwLine, token);
//                if (IsAlpha(*token)) {
//                    write(pIncFile, " ");
//                }
                if (*token != ',') {
                    nbParams++;
                    struct MACRO_TOKEN *param_token = malloc(sizeof(struct MACRO_TOKEN));
                    param_token->name = strdup(token);
                    param_token->next = NULL;
                    if (last_param_token != NULL) {
                        last_param_token->next = param_token;
                    } else {
                        param_tokens = param_token;
                    }
                    last_param_token = param_token;
                }
            }
        }

        char **params = malloc(sizeof(char*) * (nbParams + 1));
        params[nbParams] = NULL;
        for (int i = 0; param_tokens != NULL; i++) {
            params[i] = param_tokens->name;
            struct MACRO_TOKEN *p = param_tokens->next;
            free(param_tokens);
            param_tokens = p;
        }

        char buffer[256];
        buffer[0] = '\0';
        int nextAppend = 0;
        for (size_t content_i = 0; pMacroInfo->contents[content_i] != NULL; content_i++) {
            const char *currentContentToken = pMacroInfo->contents[content_i];
            if (strcmp(currentContentToken, "##") == 0) {
                nextAppend = 1;
                continue;
            }
            int param_i = FindInArray(currentContentToken, pMacroInfo->params);
            if (!nextAppend) {
                write(pIncFile, TranslateOperator(buffer));
                if (buffer[0] != '\0') {
                    write(pIncFile, " ");
                    buffer[0] = '\0';
                }
            }
            if (param_i < 0) {
                strcat(buffer, currentContentToken);
            } else {
                strcat(buffer, params[param_i]);
            }
            nextAppend = 0;
        }
        if (buffer[0]) {
            write(pIncFile, TranslateOperator(buffer));
        }
        for (int i = 0; params[i]; i++) {
            free(params[i]);
        }
        free(params);
    } else {
        debug_printf("%u: macro invocation found: %s\n", pIncFile->dwLine, pszToken);
        if (dwFlags & MF_INTERFACEEND) {
            xprintf(pIncFile, "??Interface equ <>\r\n");
            pIncFile->bIsInterface = 0;
        }

        char *token = PeekNextToken(pIncFile);
        if (token != NULL && *token == '(') {
            write(pIncFile, pszToken);
            token = GetNextToken(pIncFile);
            if (dwFlags & MF_SKIPBRACES) {
                write(pIncFile, " ");
            } else {
                write(pIncFile, token);
            }
            dwCnt = 1;
            while (1) {
                token = GetNextToken(pIncFile);
                if (token == NULL) {
                    break;
                }
                if (*token == ')') {
                    dwCnt--;
                } else if (*token == '(') {
                    dwCnt++;
                }
                if (dwCnt == 0) {
                    break;
                }
                if (dwFlags & MF_PARAMS) {
                    token = TranslateName(token, NULL, NULL);
                }
                debug_printf("%u: macro parameter: %s\n", pIncFile->dwLine, token);
                if (IsAlpha(*token)) {
                    write(pIncFile, " ");
                }
                write(pIncFile, TranslateOperator(token));
            }
            if (dwFlags & MF_SKIPBRACES) {
                write(pIncFile, " ");
            } else {
                write(pIncFile, ")");
            }
            if (dwFlags & MF_COPYLINE) {
                bPtr = 0;
                pszName = NULL;
                pszType = NULL;
                while (1) {
                    if (dwFlags & MF_PARAMS) {
                        token = GetNextToken(pIncFile);
                        if (token == NULL || strcmp(token, ";") == 0) {
                            break;
                        }
                    } else {
                        token = GetNextTokenPP(pIncFile);
                        if (token == NULL) {
                            break;
                        }
                    }
                    if (dwFlags & MF_PARAMS) {
                        if (strcmp(token, "_") == 0 || strcmp(token, ",") == 0) {
                            char *param = pszType;
                            if (param == NULL) {
                                param = pszName;
                            }
                            if (param != NULL) {
                                debug_printf("%u: MacroInvocation, param=%s\n", pIncFile->dwLine, param);
                                write(pIncFile, ", :");
                                while (bPtr != 0) {
                                    write(pIncFile, "ptr ");
                                    bPtr--;
                                }
                                write(pIncFile, TranslateType(param, g_bUntypedParams));
                            }
                            pszType = NULL;
                            pszName = NULL;
                            bPtr = 0;
                            if (strcmp(token, ",") == 0) {
                                continue;
                            }
                        }
                        if (strcmp(token, "(") == 0) {
                            dwCnt++;
                        } else if (strcmp(token, ")") == 0) {
                            dwCnt--;
                        } else if (strcmp(token, "*") == 0) {
                            bPtr++;
                        } else if (strcmp(token, "[") == 0) {
                            bPtr++;
                            while (1) {
                                token = GetNextTokenPP(pIncFile);
                                if (token == NULL || strcmp(token, "]") == 0) {
                                    break;
                                }
                            }
                        } else {
                            if (stricmp(token, "this") == 0 || stricmp(token, "this_")) {
                                continue;
                            }
                            token = ConvertTypeQualifier(token);
                            if (*token != '\0') {
                                pszType = pszName;
                                pszName = token;
                            }
                        }
                        continue;
                    }
                    write(pIncFile, token);
                }
            }
        } else {
            if (dwParms == 0) {
                write(pIncFile, pszToken);
            } else {
                dwRC = 1;
                goto exit;
            }
        }
    }
    if (dwFlags & MF_ENDMACRO) {
        pIncFile->pszEndMacro = pszToken;
        pIncFile->dwBlockLevel = pIncFile->dwBraces;
    }
done:
    if (bWriteLF) {
        write(pIncFile, "\r\n");
    }
    if ((dwFlags & MF_INTERFACEBEG) && pIncFile->pszStructName != NULL) {
        xprintf(pIncFile, "??Interface equ <%s>\r\n", pIncFile->pszStructName);
        pIncFile->bIsInterface = 1;
    }
    if (!g_bConstants) {
        pIncFile->pszOut = pszOutSave;
        pIncFile->pszOut[0] = '\0';
    }
    if (dwFlags & MF_STRUCTBEG) {
        ParseTypedefUnionStruct(pIncFile, "struct", 0);
    }
    dwRC = 1;
exit:
    return dwRC;

}

// the following types of declarations are known:
// 1. typedef (struct, enum)
// 2. extern
// 3. prototypes

int ParseC(struct INCFILE* pIncFile) {
    int bNextParm;
    int bIsClass;
    char* pszToken;
    int dwRC;

    pszToken = GetNextToken(pIncFile);
    if (pszToken == NULL) {
        debug_printf("%u: ParceC, eof reached\n", pIncFile->dwLine);
        return 0;
    }
    if (WriteComment(pIncFile)) {
        write(pIncFile, "\r\n");
    }
    if (*pszToken == ';') {
        pIncFile->pszLastToken = NULL;
        pIncFile->pszImpSpec = NULL;
        pIncFile->pszCallConv = NULL;
        pIncFile->dwQualifiers = 0;
        goto exit;
    }
#if 1
    pszToken = TranslateToken(pszToken);
#endif
    if (strcmp(pszToken, "typedef") == 0) {
        char* pszOut = pIncFile->pszOut;
        debug_printf("%u: ParseC, 'typedef' found\n", pIncFile->dwLine);
        dwRC = ParseTypedef(pIncFile);
        if (!g_bTypedefs) {
            pIncFile->pszOut = pszOut;
            *pIncFile->pszOut = '\0';
        }
        goto exit;
    }
    // "struct" may be a struct declaration, but may be a function returning
    // a struct as well. Hard to tell.
    // even more, it may be just a forward declaration! ignore that!

    int isClass;
    if (IsUnionStructClass(pszToken, &isClass)) {
        debug_printf("%u: ParseTypedef, '%s' found\n", pIncFile->dwLine, pszToken);
        if (!IsFunction(pIncFile)) {
            char* pszOut = pIncFile->pszOut;
            dwRC = ParseTypedefUnionStruct(pIncFile, pszToken, isClass);
            if (!g_bTypedefs) {
                pIncFile->pszOut = pszOut;
                *pIncFile->pszOut = '\0';
            }
            goto exit;
        }
        debug_printf("%u: ParceC, 'union/struct' ignored (function return type)\n", pIncFile->dwLine);
    }
    if (strcmp(pszToken, "extern") == 0) {
        if (!IsFunction(pIncFile)) {
            char* pszOut = pIncFile->pszOut;
            debug_printf("%u: ParceC, 'extern' found\n", pIncFile->dwLine);
            ParseExtern(pIncFile);
            if (!g_bExternals) {
                pIncFile->pszOut = pszOut;
                *pIncFile->pszOut = '\0';
            }
        }
        goto exit;
    }
    if (strcmp(pszToken, "enum") == 0) {
        debug_printf("%u: ParceC, 'enum' found\n", pIncFile->dwLine);
        dwRC = ParseTypedefEnum(pIncFile, 0);
        goto exit;
    }

    // first check if name is a known prototype qualifier
    // this may also be a macro, but no macro invocation should
    // be generated then

    struct ITEM_STRINT* protoQual = CheckProtoQualifier(pIncFile, pszToken);
    if (protoQual != NULL) {
        debug_printf("%u: ParseC, prototype qualifier '%s' found, value=%X\n", pIncFile->dwLine, pszToken, (int)protoQual->value);
        if (protoQual->value & FQ_IMPORT) {
            pIncFile->pszImpSpec = pszToken;
        } else if (protoQual->value == FQ_STDCALL || protoQual->value == FQ_CDECL || protoQual->value == FQ_SYSCALL || protoQual->value == FQ_PASCAL) {
            pIncFile->pszCallConv = pszToken;
        }
        pIncFile->dwQualifiers |= protoQual->value;
        goto exit;
    }
    if (pIncFile->dwQualifiers == 0) {
        struct ITEM_MACROINFO* macroInfo = IsMacro(pIncFile, pszToken);
        if (macroInfo != 0) {
            if (MacroInvocation(pIncFile, pszToken, macroInfo, 1)) {
                goto exit;
            }
        }
    }

    if (*pszToken == '(') {
        if (pIncFile->pszLastToken != NULL) {
            debug_printf("%u: ParceC, prototype found\n", pIncFile->dwLine);
            char* pszOut = pIncFile->pszOut;
            ParsePrototype(pIncFile, pIncFile->pszLastToken, pIncFile->pszImpSpec, pIncFile->pszCallConv);
            if (!g_bPrototypes) {
                pIncFile->pszOut = pszOut;
                *pIncFile->pszOut = '\0';
            }
            goto exit;
        }
    }

    if (IsName(pIncFile, pszToken)) {
        pIncFile->pszLastToken = pszToken;
        debug_printf("%u: token %s found\n", pIncFile->dwLine, pszToken);
    } else {
        if (strcmp(pszToken, "{") == 0) {
            pIncFile->dwBraces++;
            if (pIncFile->pszOut[-1] == '\n') {
                write(pIncFile, ";{\r\n");
            }
            debug_printf("%u: begin block, new level=%u\n", pIncFile->dwLine, pIncFile->dwBraces);
        } else if (strcmp(pszToken, "}") == 0) {
            pIncFile->dwBraces--;
            if (pIncFile->pszOut[-1] == '\n') {
                write(pIncFile, ";}\r\n");
            }
            if (pIncFile->pszEndMacro != NULL && pIncFile->dwBraces == pIncFile->dwBlockLevel) {
                xprintf(pIncFile, "%s_END\r\n\r\n", pIncFile->pszEndMacro);
                pIncFile->dwBlockLevel = 0;
                pIncFile->pszEndMacro = NULL;
            }
        }
    }

exit:
    if (g_bIncludeComments && g_szComment[1] != '\0') {
        write(pIncFile, g_szComment);
        g_szComment[0] = '\0';
        write(pIncFile, "\r\n");
    }
    return 1;
}

// ---------------------------------------------------

void AnalyzerIncFile(struct INCFILE* pIncFile) {
    struct stat statbuf;

    debug_printf("Analyzer@IncFile begin %s\n", pIncFile->pszFileName);
#ifdef _DEBUG
    FILE* f = fopen("~parser.tmp", "w");
    if (f != NULL) {
        fwrite(pIncFile->pBuffer2, pIncFile->pszOut - pIncFile->pBuffer2, 1, f);
        fclose(f);
    }
#endif
    pIncFile->pszInStart = pIncFile->pszIn = pIncFile->pBuffer2;
    pIncFile->pszOutStart = pIncFile->pszOut = pIncFile->pBuffer1;
    pIncFile->pszOut[0] = '\0';
    pIncFile->bComment = 0;
    pIncFile->bDefinedMac = 0;
    pIncFile->bAlignMac = 0;
    pIncFile->bSkipPP = 0;
    pIncFile->dwLine = 1;
    pIncFile->bNewLine = 1;

    if (g_pStructures == NULL) {
        g_pStructures = CreateList(MAXITEMS, sizeof(void*));
        // AddItemArrayList(g_pStructures, (struct NAMEITEM*) g_KnownStructures.pItems, g_KnownStructures.numItems);
    }

    if (g_pStructureTags == NULL) {
        g_pStructureTags = CreateList(MAXITEMS, sizeof(void*));
        // AddItemArrayList(g_pStructures, (struct NAMEITEM*) g_KnownStructures.pItems, g_KnownStructures.numItems);
    }
    if (g_pMacros == NULL) {
        g_pMacros = CreateList(MAXITEMS, sizeof(struct ITEM_MACROINFO));
    }
#if PROTOSUMMARY
    if (g_bProtoSummary && g_pPrototypes == NULL) {
        g_pPrototypes = CreateList(MAXITEMS, sizeof(void*));
    }
#endif
#if TYPEDEFSUMMARY
    if (g_bTypedefSummary && g_pTypedefs == NULL) {
        g_pTypedefs = CreateList(MAXITEMS, sizeof(void*));
    }
#endif
#if DYNPROTOQUALS
    if (g_pQualifiers == NULL) {
        g_pQualifiers = CreateList(0x400, sizeof(struct LISTITEM));
        AddItemArrayList(g_pQualifiers, (struct NAMEITEM*) g_ProtoQualifiers.pItems, g_ProtoQualifiers.numItems);
    }
#endif
    if (g_bCreateDefs) {
        pIncFile->pDefs = CreateList(MAXITEMS, sizeof(char*));
    }

#ifdef INCLUDE_GENERATOR_INFO
    write(pIncFile, ";--- include file created by h2incc " VERSION " (" COPYRIGHT ")\r\n");
    stat(pIncFile->pszFullPath, &statbuf);
    xprintf(pIncFile, ";--- source file: %s, last modified: %u-%u-%u %u:%u\r\n", pIncFile->pszFullPath, 1900 + pIncFile->filetime.tm_year, pIncFile->filetime.tm_mon, pIncFile->filetime.tm_mday, pIncFile->filetime.tm_hour, pIncFile->filetime.tm_min);
    write(pIncFile, ";--- cmdline used for creation:");

    for (int i = 1; i < g_argc; i++) {
        write(pIncFile, " ");
        write(pIncFile, g_argv[i]);
    }
    write(pIncFile, "\r\n\r\n");
#endif

    int dwRC;
    do {
        dwRC = ParseC(pIncFile);
    } while (dwRC != 0);

#ifdef INCLUDE_GENERATOR_INFO
    if (pIncFile->bIfLvl != 0) {
        fprintf(stderr, "%s, %u: unmatching if/endif\n", pIncFile->pszFileName, pIncFile->dwLine);
        pIncFile->dwErrors++;
    }
    write(pIncFile, "\r\n");
    if (pIncFile->dwWarnings != 0) {
        xprintf(pIncFile, "--- warnings: %u\r\n", pIncFile->dwWarnings);
    }
    xprintf(pIncFile, ";--- errors: %u\r\n", pIncFile->dwErrors);
    xprintf(pIncFile, ";--- end of file ---\r\n");
#endif

    debug_printf("Analyzer@IncFile end %s\n", pIncFile->pszFileName);
}

void DestroyAnalyzerData(void) {
    debug_printf("Destroying analyzer data\n");
    if (g_pStructures != NULL) {
        DestroyList(g_pStructures);
        g_pStructures = NULL;
    }
    if (g_pStructureTags != NULL) {
        DestroyList(g_pStructureTags);
        g_pStructureTags = NULL;
    }
    if (g_pMacros != NULL) {
        DestroyList(g_pMacros);
        g_pMacros = NULL;
    }
#if PROTOSUMMARY
    if (g_pPrototypes != NULL) {
        DestroyList(g_pPrototypes);
        g_pPrototypes = NULL;
    }
#endif
#if TYPEDEFSUMMARY
    if (g_pTypedefs != NULL) {
        DestroyList(g_pTypedefs);
        g_pTypedefs = NULL;
    }
#endif
#if DYNPROTOQUALS
    if (g_pQualifiers != NULL) {
        DestroyList(g_pQualifiers);
        g_pQualifiers = NULL;
    }
#endif
}

// parser subroutines

// parser: skip comments "/* ... */" in a line

void skipcomments(struct INCFILE* pIncFile, char* pszLine) {
    char szChar[2];
#define SKIPCOMMENT_READCHAR(NEWCHAR) do {      \
        szChar[0] = szChar[1];                  \
        szChar[1] = (NEWCHAR);                  \
    } while (0)

    char* os = pIncFile->pszOut;
    if (g_bIncludeComments) {
        *os++ = PP_COMMENT;
    }

    char* is = pszLine;
    szChar[1] = '\0';

    while (1) {
        SKIPCOMMENT_READCHAR(*is);
        is++;

        if (szChar[1] == '\0') {
            break;
        }
        if (strncmp(szChar, "//", 2) == 0 && !pIncFile->bComment) {
            break;
        }
        if (pIncFile->bComment && strncmp(szChar, "*/", 2) == 0) {
            is[-2] = ' ';
            is[-1] = ' ';
            pIncFile->bComment = 0;
        }
        if (strncmp(szChar, "/*", 2) == 0 && !pIncFile->bComment) {
            is[-2] = ' ';
            is[-1] = ' ';
            pIncFile->bComment = 1;
        }
        if (pIncFile->bComment) {
            if (g_bIncludeComments) {
                *os++ = szChar[1];
            }
            is[-1] = ' ';
        }
    }
    if (g_bIncludeComments) {
        if (os[-1] == (char)PP_COMMENT) {
            os[-1] = '\0';
        } else {
            *os++ = '\0';
            pIncFile->pszOut = os;
        }
    }
}

// ConvertNumber: esi = input stream, edi = output stream
// converts number from C syntax to MASM syntax
// must preserve ecx edx

void ConvertNumber(char** pOs, char** pIs) {
    int flags;
    int nb = 0;
    char* is = *pIs;
    char* os = *pOs;

    flags = 0x0;
    if (strnicmp(is, "0x", 2) == 0) {
        is += 2;
        if (*is > '9') {
            *os++ = '0';
        }
        flags |= 0x1;
    }
    int len = 0;
    while (1) {
        char c = *is;
        if (!IsAlphaNumeric(c)) {
            if (c == '.') {
                flags |= 0x2;
            } else {
                break;
            }
        }
        *os++ = *is++;
        len++;
    }
    uint8_t o = 0;
    if (len > 2) {
        if (strnicmp(&os[-2], "i8", 2) == 0) {
            len -= 2;
            os -= 2;
            o++;
        }
    } else if (len > 3) {
        if (strnicmp(&os[-3], "i16", 3) == 0 || strnicmp(&os[-3], "i32", 3) == 0 || strnicmp(&os[-3], "i64", 3) == 0) {
            len -= 3;
            os -= 3;
            o++;
        }
    } else if (len > 4) {
        if (strnicmp(&os[-4], "i128", 4) == 0) {
            len -= 4;
            os -= 4;
            o++;
        }
    }
    if (o) {
        if (strnicmp(&os[-1], "u", 1) == 0) {
            len -= 1;
            os--;
            goto skip1;
        }
    }
    if (len > 1) {
        if (strnicmp(&os[-1], "u", 1) == 0 || strnicmp(&os[-1], "l", 1) == 0) {
            os--;
            len--;
            if (len > 1 && strnicmp(&os[-1], "u", 1) == 0) {
                os--;
                len--;
            }
        } else if (strnicmp(&os[-1], "e", 1) == 0) {
            if (flags & 0x2) {
                os[-1] = 'E';
                if (*is == '-' || *is == '+') {
                    *os++ = *is++;
                    while (1) {
                        char c = *is;
                        if (c >= 'A') {
                            c |= 0x20;
                        }
                        if ((c >= '0' && c<= '9') || ( c == 'f' || c == 'l')) {
                            *os++ = *is++;
                        } else {
                            break;
                        }
                    }
                }
            }
        }
        if (flags & 0x2) {
            if (strnicmp(&os[-1], "f", 1) == 0 || strnicmp(&os[-1], "l", 1) == 0) {
                os--;
            }
        }
    }
skip1:
    if (flags & 0x1) {
        *os++ = 'h';
    }
    *pIs = is;
    *pOs = os;
}

#define szBell      "07"
#define szBackSp    "08"
#define szHTab      "09"
#define szLF        "0a"
#define szVTab      "0b"
#define szFF        "0c"
#define szCR        "0d"
#define szDQUOTES   "22"
#define szLBRACKET  "7b"
#define szRBRACKET  "7d"


// preserve ecx, edx!
// ch: bit 0=1: previous item is enclosed in '"'
//     bit 1=1: no previous char

#define STRINGLIT_PREVITEM_DQUOTED  0x01
#define STRINGLIT_NOPREVCHAR        0x02

void addescstr(char** pOs, char* value, uint8_t flags) {
    char* os = *pOs;
    if (flags == 0) {
        *os++ = '"';
    }
    if ((flags & STRINGLIT_NOPREVCHAR) == 0) {
        *os++ = ',';
    }
    *os++ = value[0];
    *os++ = value[1];
    *os++ = 'h';
    *pOs = os;
}

void GetCharLiteral(char** pOs, char** pIs) {
    char* os = *pOs;
    char* is = *pIs;

    uint8_t flags = STRINGLIT_NOPREVCHAR;
    int value;
    char c;
    c = *is++;
    if (c == '\\') {
        c = *is++;
        if (c >= '0' && c <= '7') {
            value = c;
            uint8_t nb = 0x3;
            while (nb != 0) {
                c = *is++;
                if (c >= '0' && c <= '7') {
                    value = value * 8 + c;
                } else {
                    break;
                }
            }
        } else if (c == 'a') {
            value = '\a';
        } else if (c == 'b') {
            value = '\b';
        } else if (c == 'f') {
            value = '\f';
        } else if (c == 'n') {
            value = '\n';
        } else if (c == 'r') {
            value = '\r';
        } else if (c == 't') {
            value = '\t';
        } else if (c == 'v') {
            value = '\v';
        } else if (c == 'x') {
            uint8_t nb = 3;
            while (nb != 0) {
                c = *is;
                c |= 0x20;
                if (c >= '0' && c <= '9') {
                    value = value * 16 + c - '0';
                } else if (c >= 'a' && c <= 'f') {
                    value = value * 16 + c - 'a';
                } else {
                    break;
                }
                is++;
                nb--;
            }
        } else {
            value = c;
        }
    } else {
        value = c;
    }
    char buffer[8];
    sprintf(buffer, "%d", value);

    c = *is++;
    while (c != '\'') {
        c = *is++;
    }

    *pIs = is;
    strcpy(os, buffer);
    os += strlen(buffer);
    *pOs = os;
}

//if (c == '}') {
//addescstr(&os, szRBRACKET, flags);
void GetStringLiteral(char** pOs, char** pIs) {
    char* os = *pOs;
    char* is = *pIs;

    uint8_t flags = STRINGLIT_NOPREVCHAR;
    char c;
    do {
        c = *is++;
        if (c == '\\') {
            c = *is++;
            if (c >= '0' && c <= '7') {
                if (flags == 0x0) {
                    *os++ = '"';
                }
                *os++ = ',';
                is--;
                uint8_t nb = 0x3;
                while (nb != 0) {
                    c = *is++;
                    if (c >= '0' && c <= '7') {
                        *os++ = c;
                    } else {
                        break;
                    }
                    is++;
                    nb--;
                }
                if (nb != 3) {
                    *os++ = 'o';
                }
            } else if (c == 'a') {
                addescstr(&os, szBell, flags);
            } else if (c == 'b') {
                addescstr(&os, szBackSp, flags);
            } else if (c == 'f') {
                addescstr(&os, szFF, flags);
            } else if (c == 'n') {
                addescstr(&os, szLF, flags);
            } else if (c == 'r') {
                addescstr(&os, szCR, flags);
            } else if (c == 't') {
                addescstr(&os, szHTab, flags);
            } else if (c == 'v') {
                addescstr(&os, szVTab, flags);
            } else if (c == 'x') {
                if (flags == 0) {
                    *os++ = '"';
                }
                *os++ = ',';
                uint8_t nb = 3;
                while (nb != 0) {
                    c = *is;
                    c |= 0x20;
                    if (c >= '0' && c <= '9') {
                        *os++ = c;
                    } else if (c >= 'a' && c <= 'f') {
                        if (nb == 3) {
                            *os++ = '0';
                        }
                        *os++ = c;
                    } else {
                        break;
                    }
                    is++;
                    nb--;
                }
                if (nb != 3) {
                    *os++ = 'h';
                }
            } else if (c == '"') {
                if (flags != 0) {
                    *os++ = ',';
                    *os++ = '"';
                }
                *os++ = c;
                *os++ = c;
                flags = 0;
                continue;
            } else {
                goto normalchar;
            }
            flags = STRINGLIT_PREVITEM_DQUOTED;
            continue;
        }
normalchar:
        if (flags != 0) {
            if (c == '"') {
                break;
            } else {
                if (flags & STRINGLIT_PREVITEM_DQUOTED) {
                    *os++ = ',';
                }
                *os++ = '"';
            }
        }
        if (c == '\0') {
            is--;
            c = '"';
        }
        *os++ = c;
        flags = 0;
    } while (c != '"');
    // dont add terminating 0!
    // this won't work for strings as macro
    // parameters: DECLSPEC_GUID("xxxxxxxx-xxxx...")
#if ADDTERMNULL
    *os++ = '0';
    *os++ = ',';
#endif
    *pIs = is;
    *pOs = os;
}

void GetWStringLiteral(char** pOs, char** pIs) {
    char* is = *pIs;
    char* os = *pOs;

    *os++ = '(';
    is++;
    GetStringLiteral(&os,&is);
    *os++ = ')';

    *pIs = is;
    *pOs = os;
}

// parse a source line

void parseline(struct INCFILE* pIncFile, char* pszLine, int bWeak) {
    int bIsPreProc;
    int bIsDefine;

    bIsPreProc = 0;
    bIsDefine = 0;
    char* is = pszLine;
    if (*is == '#') {
        bIsPreProc = 1;
    }
    skipcomments(pIncFile, is);
    char* os = pIncFile->pszOut;
    uint32_t tokenCounter = 0;  // token counter
    while (1) {
        char c = *is;
        if (c == '\0') {
            break;
        }
        char* start_token = os; // holds start of token
        if (c == '/' && is[1] == '/') {
            if (g_bIncludeComments) {
                *os++ = PP_COMMENT;
                strcpy(os, is);
                os += strlen(os) + 1;
            }
            break;
        }
        // get 1 token
        while (1) {
            if (*is == '\0') {
                break;
            }
            c = *is++;
            if (c == ' ' || c == '\t') {
                break;
            }
            if (os == start_token && c == '"') {
                GetStringLiteral(&os, &is);
                break;
            }
            if (os == start_token && c == '\'') {
                GetCharLiteral(&os, &is);
                break;
            }
            if (os == start_token && c == 'L' && *is == '"') {
                GetWStringLiteral(&os, &is);
                break;
            }
#if 1
            if (os == start_token && c >= '0' && c <= '9') {
                is--;
                ConvertNumber(&os, &is);
                break;
            }
#endif
            if (IsDelim(c)) {
                if (os != start_token) {
                    if (c == '(' && bIsDefine && tokenCounter == 2) {
                        *os++ = '\0';
                        *os++ = PP_MACRO;
                    }
                    is--;
                } else {
                    *os++ = c;
                    if (IsTwoCharOp(c, *is)) {
                        *os++ = *is++;
                    }
                }
                break;
            }
            *os++ = c;
        }
        if (start_token != os) {
            *os++ = '\0';
            tokenCounter++;
            if (tokenCounter == 2 && bIsPreProc) {
                if (strncmp(start_token, "define", 6) == 0) {
                    bIsDefine = 1;
                }
            }
        }
    }
    char cc;
    if (bWeak) {
        cc = PP_WEAKEOL;
    } else {
        cc = PP_EOL;
    }
    *os++ = cc;
    *os++ = '\0';
    pIncFile->pszOut = os;
}

// get a source text line
// 1. skip any white spaces at the beginning
// 2. check '\' for preprocessor lines (weak EOL)
// 3. call parseline
// 4. adjust m_pszIn
// return line length in eax (0 is EOF)

size_t Parse_Line(struct INCFILE* pIncFile) {
    char* is = pIncFile->pszIn;
    while (*is == ' ' && *is == '\t') {
        is++;
    }
    char* origIs = is;
    char* lineEnd = NULL;

    while (1) {
        if (*is == '\0') {
            break;
        }
        char c = *is++;
        if (c == '\r' || c == '\n') {
            is[-1] = '\0';
            if (lineEnd == NULL) {
                lineEnd = &is[-1];
            }
            if (c == '\n') {
                break;
            }
        }
    }
    if (is != pIncFile->pszIn) {
        pIncFile->dwLine++;
    }

    int weak = 0;
    if (pIncFile->bContinuation || *origIs == '#') {
        pIncFile->bContinuation = 0;
        while (lineEnd >= origIs) {
            char c = *lineEnd;
            if (c == '\\') {
                while (*lineEnd != '\0') {
                    *lineEnd = ' ';
                    lineEnd++;
                }
                weak = 1;
                pIncFile->bContinuation = 1;
                break;
            }
            if (c >= ' ') {
                break;
            }
            lineEnd--;
        }
    }

    parseline(pIncFile, origIs, weak);
    size_t res = is - pIncFile->pszIn;
    pIncFile->pszIn = is;
    return res;
}

//  the parser
//  input is C header source
//  output is tokenized, that is:
//  + each token is an asciiz string
//  + numeric literals (numbers) are converted to ASM already
//  + comments are marked as such
//  example:
//  input: "#define VAR1 0xA+2"\r\n
//  output: "#define",0,"VAR1",0,"0Ah",0,"+",0,"2",0,PP_EOL,0

void ParserIncFile(struct INCFILE* pIncFile) {
    pIncFile->dwLine = 1;
    pIncFile->bContinuation = 0;
    int nb_chars;
    do {
        nb_chars = Parse_Line(pIncFile);
    } while (nb_chars != 0);
    *pIncFile->pszOut = '\0';
}

// write output buffer to file
// eax=0 if error

int WriteIncFile(struct INCFILE* pIncFile, char* pszFileName) {
    int rc;
    FILE* file;

    rc = 1;
    if (pIncFile->dwErrors != 0) {
        fprintf(stderr, "%d errors occurred while parsing %s. Skipping writing files.\n", pIncFile->dwErrors, pIncFile->pszFileName);
        return 0;
    }

    size_t lenBuffer1 = strlen(pIncFile->pBuffer1);
    if (lenBuffer1) {
        if (pszFileName[0] == '\0') {
            file = stdout;
        } else {
            file = fopen(pszFileName, "w");
        }
        if (file == NULL) {
            fprintf(stderr, "cannot create file %s\n", pszFileName);
            rc = 0;
        } else {
            size_t actual = fwrite(pIncFile->pBuffer1, 1, lenBuffer1, file);
            if (actual != lenBuffer1) {
                fprintf(stderr, "%s: write error\n", pszFileName);
                rc = 0;
            }
            if (pszFileName[0] == '\0') {
            } else {
                fclose(file);
            }
        }
    }
    return rc;
}

// write to file
// eax=0 if error

int WriteDefIncFile(struct INCFILE* pIncFile, char* pszFileName) {
    FILE* file;
    int rc;
    char szFile[MAX_PATH];
    char szText[512];

    rc = 0;
    if (pIncFile->pDefs == 0) {
        // fprintf(stderr, "no .DEF file requested\n");
        goto exit;
    }
    if (GetNumItemsList(pIncFile->pDefs) == 0) {
        if (g_bWarningLevel > 2) {
            fprintf(stderr, "no items for .DEF file\n");
        }
        goto exit;
    }

    strcpy(szFile, pszFileName);
    size_t lenFileName = strlen(pszFileName);
    if (lenFileName < 5 || szFile[lenFileName-4] != '.') {
        fprintf(stderr, "invalid file name %s for .DEF file\n", pszFileName);
        goto exit;
    }
    strcpy(szFile+lenFileName-3, "def");

    SortCSList(pIncFile->pDefs);

    file = fopen(pszFileName, "w");
    if (file != NULL) {
        fprintf(file, "LIBRARY\r\n");
        fprintf(file, "EXPORTS\r\n");
        struct NAMEITEM* item = (struct NAMEITEM*) GetNextItemList(pIncFile->pDefs, 0);
        while (item != NULL) {
            fprintf(file, " \"%s\"\r\n", item->pszName);
            item = GetNextItemList(pIncFile->pDefs, item);
        }
        fclose(file);
        rc = 1;
    } else {
        fprintf(stderr, "cannot create file %s\n", pszFileName);
    }
exit:
    return rc;
}


char* GetFileNameIncFile(struct INCFILE* pIncFile, uint32_t* dwLine) {
    *dwLine = pIncFile->dwLine;
    return pIncFile->pszFileName;
}

char* GetFullPathINcFile(struct INCFILE* pIncFile) {
    return pIncFile->pszFullPath;
}

uint32_t GetLineIncFile(struct INCFILE* pIncFIle) {
    return pIncFIle->dwLine;
}

struct INCFILE* GetParentIncFile(struct INCFILE* pIncFile) {
    return pIncFile->pParent;
}

// constructor include file object
// returns:
//  eax = 0 if error occured
//  eax = _this if ok

struct INCFILE* CreateIncFile(char* pszFileName, struct INCFILE* pParent) {
    FILE* file;
    size_t dwFileSize;
    struct INCFILE* pIncFile;

    pIncFile = malloc(sizeof(struct INCFILE));
    if (pIncFile == NULL) {
        goto exit;
    }
    memset(pIncFile, 0, sizeof(struct INCFILE));
    file = fopen(pszFileName, "r");
    if (file == NULL) {
        if (g_pszIncDir) {
            // _makepath(szFileName, NULL, g_pszIncDir, g_szName, g_szExt);
            // file = fopen(szFileName, "r");
            if (file != NULL) {
                // pszFileName = szFileName;
                goto file_exists;
            }
        }
        if (pParent != NULL) {
            uint32_t parentLine;
            char* parentFileName = GetFileNameIncFile(pParent, &parentLine);
            fprintf(stderr, "%s, %u: ", parentFileName, parentLine);
        }
        fprintf(stderr, "cannot open file %s\n", pszFileName);
        free(pIncFile);
        pIncFile = NULL;
        goto exit;
    }
file_exists:
    pIncFile->pszFullPath = AddString(pszFileName);
    // _makepath(szFileName, NULL, NULL, g_szName, g_szExt);
    // pIncFile->pszFileName = AddString(szFileName, 0);
    pIncFile->pszFileName = AddString(pszFileName);

    struct stat fileStat;
    stat(pszFileName, &fileStat);
    gmtime_r(&fileStat.st_mtime, &pIncFile->filetime);

    dwFileSize = fileStat.st_size;

    uint32_t extraBuffer;
#if ADD50PERCENT
    extraBuffer = extraBuffer >> 1;    // add 50% to file size for buffer size
#else
    extraBuffer = dwFileSize;
#endif
    pIncFile->dwBufSize = dwFileSize + extraBuffer;

    pIncFile->pBuffer1 = malloc(pIncFile->dwBufSize);
    debug_printf("alloc buffer 1 for %s returned %p\n", pIncFile->pszFileName, pIncFile->pBuffer1);
    pIncFile->pBuffer2 = malloc(pIncFile->dwBufSize);
    debug_printf("alloc buffer 2 for %s returned %p\n", pIncFile->pszFileName, pIncFile->pBuffer2);
    if (pIncFile->pBuffer1 == NULL || pIncFile->pBuffer2 == NULL) {
        fprintf(stderr, "fatal error: out of memory\n");
        free(pIncFile);
        g_bTerminate = 1;
        pIncFile = NULL;
        goto exit;
    }
    fread(pIncFile->pBuffer1, 1, dwFileSize, file);
    fclose(file);
    pIncFile->pBuffer1[dwFileSize] = '\0';
    pIncFile->pBuffer1[dwFileSize+1] = '\0';
    pIncFile->pszInStart = pIncFile->pszIn = pIncFile->pBuffer1;
    pIncFile->pszOutStart = pIncFile->pszOut = pIncFile->pBuffer2;
    pIncFile->pszOut[0] = '\0';
    pIncFile->pParent = pParent;
    pIncFile->bNewLine = 1;
exit:
    return pIncFile;
}

// destructor include file object

void DestroyIncFile(struct INCFILE* pIncFile) {
    free(pIncFile->pBuffer1);
    free(pIncFile->pBuffer2);
    if (pIncFile->pDefs != NULL) {
        DestroyList(pIncFile->pDefs);
        pIncFile->pDefs = NULL;
    }
    DestroyString(pIncFile->pszFullPath);
    DestroyString(pIncFile->pszFileName);

    free(pIncFile);
}


