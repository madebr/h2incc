#ifndef LIST_H
#define LIST_H

#include <stdint.h>

struct NAMEITEM;
struct LIST;

struct LISTITEM {
    char* name;
    union {
        uint32_t u32;
        char* pStr;
        void* pVoid;
    } value;
};

struct LIST* CreateList(uint32_t, uint32_t);
void DestroyList(struct LIST*);
void SortList(struct LIST*);
void SortCSList(struct LIST*);
void* AddItemList(struct LIST*, char* pszName);
void* AddItemArrayList(struct LIST*, struct NAMEITEM* pItem, uint32_t dwItems);
void* GetItemList(struct LIST*, struct NAMEITEM* pItem);
void* FindItemList(struct LIST*, char* pszName);
uint32_t GetItemSizeList(struct LIST*);
uint32_t GetNumItemsList(struct LIST*);

void* list_bsearch(void* key, void* base, uint32_t num, uint32_t width, int(*compare)(const void*, const void*), void** res);

#endif
