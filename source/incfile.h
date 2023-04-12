#ifndef INCFILE_H
#define INCFILE_H

#include <stdint.h>

#define MAXIFLEVEL 31

struct INCFILE;

struct INCFILE* CreateIncFile(const char*, struct INCFILE*);
void DestroyIncFile(struct INCFILE*);
int WriteIncFile(struct INCFILE*, char*);
int WriteDefIncFile(struct INCFILE*, char*);
void ParserIncFile(struct INCFILE*);
void AnalyzerIncFile(struct INCFILE*);
void DestroyAnalyzerData(void);
char* GetFileNameIncFile(struct INCFILE* pFile, uint32_t* dwLine);
void GetFullPathIncFile(struct INCFILE*);
// void GetLineIncFile(struct INCFILE*);
struct INCFILE* GetParentIncFile(struct INCFILE*);
#endif
