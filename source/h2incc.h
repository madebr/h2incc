#ifndef H2INCC_H
#define H2INCC_H

#include <stdint.h>

#include "vector.h"

#define VERSION     "v0.0.1"
#define COPYRIGHT   "copyright 2022 madebr, copyright 2005-2009 japheth"

#define PROTOSUMMARY    1
#define TYPEDEFSUMMARY  1
#define DYNPROTOQUALS   1

// Prototype qualifiers

enum {
    FQ_CDECL	= 0x01,     // __cdecl
    FQ_STDCALL	= 0x02,     // __stdcall
    FQ_INLINE	= 0x04,     // __inline
    FQ_IMPORT	= 0x08,     // __declspec(dllimport)
    FQ_SYSCALL	= 0x10,     // __syscall
    FQ_PASCAL	= 0x20,     // __pascal
    FQ_STATIC	= 0x100,    // static
};

// typedefs + structures

struct SORTARRAY {
    char** pItems;
    uint32_t numItems;
};

// name item. structures or macros declared in headers
// are saved in a list so h2incc can decide if
// a type is a structure or not

struct NAMEITEM {
    char* pszName;          // ptr to item name
};

struct ITEM_STRSTR {
    char* key;
    char* value;
};

struct ITEM_STRINT {
    char* key;
    intptr_t value;
};

struct MACRO_TOKEN {
    char *name;
    struct MACRO_TOKEN *next;
};

struct ITEM_MACROINFO {
    char *key;
    intptr_t flags;
    intptr_t containsAppend;
    const char **params;
    const char **contents;
};

int cmpproc(const void*, const void*);
char* AddString(char* pszString);
void DestroyString(char* pszString);

extern int g_argc;
extern char** g_argv;
extern char** g_envp;

extern char g_szDrive[4];
extern char g_szDir[256];
extern char g_szName[256];
extern char g_szExt[256];

extern struct vector *g_pszIncDirs;
extern uint32_t g_dwStructSuffix;
extern uint32_t g_dwDefCallConv;
extern struct LIST* g_pStructures;
extern struct LIST* g_pStructureTags;
extern struct LIST* g_pMacros;
#if PROTOSUMMARY
extern struct LIST* g_pPrototypes;
#endif
#if TYPEDEFSUMMARY
extern struct LIST* g_pTypedefs;
#endif
extern struct LIST* g_pQualifiers;
extern struct SORTARRAY g_ReservedWords;
extern struct SORTARRAY g_KnownStructures;
extern struct SORTARRAY g_ProtoQualifiers;
extern char** g_ppSimpleTypes;
extern struct ITEM_MACROINFO*   g_ppKnownMacros;
extern struct ITEM_STRSTR*      g_ppTypeAttrConv;
extern struct ITEM_STRSTR*      g_ppConvertTokens;
extern struct ITEM_STRSTR*      g_ppConvertTypes1;
extern struct ITEM_STRSTR*      g_ppConvertTypes2;
extern struct ITEM_STRSTR*      g_ppConvertTypes3;
extern struct ITEM_STRSTR*      g_ppAlignments;
extern struct ITEM_STRINT*      g_ppTypeSize;

extern uint8_t g_bTerminate;

extern uint8_t g_bAddAlign;
extern uint8_t g_bBatchmode;
extern uint8_t g_bUseDefProto;
extern uint8_t g_bIncludeComments;
extern uint8_t g_bAssumeDllImport;
//extern uint8_t g_bSuppressExists;
extern uint8_t g_bIgnoreDllImport;
extern uint8_t g_bProcessInclude;
extern uint8_t g_bUntypedMembers;
extern uint8_t g_bProtoSummary;
extern uint8_t g_bNoRecords;
extern uint8_t g_bRecordsInUnions;
extern uint8_t g_bSummary;
extern uint8_t g_bTypedefSummary;
extern uint8_t g_bUntypedParams;
extern uint8_t g_bVerbose;
extern uint8_t g_b64bit;
extern uint8_t g_bWarningLevel;
extern uint8_t g_bOverwrite;
extern uint8_t g_bCreateDefs;
extern uint8_t g_bPrefixReserved;

extern uint8_t g_bPrototypes;
extern uint8_t g_bTypedefs;
extern uint8_t g_bConstants;
extern uint8_t g_bExternals;

#ifdef _TRACE
int debug_printf(const char* format, ...);
#else
#define debug_printf
#endif

#endif // H2INCC_H
