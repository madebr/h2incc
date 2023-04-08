#include "list.h"
#include "h2incc.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIST_START(L) (&(((struct LIST*)(L))[1]))

struct LIST {
    void *pFree;        // next free item in this block
    void *pMax;         // end of this list block
    uint32_t dwSize;    // size of 1 item in list
};

static uint32_t GetNumItems(const struct LIST* pList) {
    size_t size = (char*)pList->pFree - (char*)LIST_START(pList);
    return size / pList->dwSize;
}

// unlike the standard CRT bsearch this bsearch returns
// in `res` the first item > key

void* list_bsearch(void* key, void* base, uint32_t num, uint32_t width, int(*compare)(const void*, const void*), void** res) {
    char* lo = base;
    char* hi = (char*)base + num * width;
    int diff = -1;
    if (num == 0) {
        if (res != NULL) {
            *res = lo;
        }
        return NULL;
    }
    uint32_t num_left = num;
    while (1) {
        uint32_t half = num_left / 2;
        char* mid = lo + half * width;
        diff = compare(&key, mid);
        if (diff >= 0) {
            num_left = num_left - half - 1;
            lo = mid + width;
        } else {
            num_left = half;
            hi = mid;
        }
        if (num_left == 0) {
            if (lo > base && compare(&key, lo - width) == 0) {
                if (res != NULL) {
                    *res = lo;
                }
                return lo - width;
            }
            if ((char*)lo >= (char*)base + num * width) {
                if (res != NULL) {
                    *res = base + num * width;
                }
                return NULL;
            }
            diff = compare(&key, lo);
            if (diff == 0) {
                if (res != NULL) {
                    *res = lo + width;
                }
                return lo;
            } else if (diff > 0) {
                if (res != NULL) {
                    *res = lo + width;
                }
                return NULL;
            } else {
                if (res != NULL) {
                    *res = lo;
                }
                return NULL;
            }
        }
    }
}

struct LIST* CreateList(uint32_t numItems, uint32_t itemSize) {
    struct LIST* pList = malloc(sizeof(struct LIST) + itemSize * numItems);
    if (pList == NULL) {
        return NULL;
    }
    pList->pFree = LIST_START(pList);
    pList->dwSize = itemSize;
    pList->pMax = (char*)pList + sizeof(struct LIST) + itemSize * numItems;
    return pList;
}

void DestroyList(struct LIST* pList) {
    if (pList != NULL) {
        free(pList);
    }
}

static int cmpproc2(const void* p1, const void* p2) {
    return strcmp(((struct NAMEITEM*)p1)->pszName, ((struct NAMEITEM*)p2)->pszName);
}

// add an item in a list
// return: inserted item or NULL

void* AddItemList(struct LIST* pList, char* pItem) {
    if (pList->pFree == pList->pMax) {
        abort();
    }
    struct NAMEITEM tmpItem;
    tmpItem.pszName = pItem;
    char* newpos = NULL;
    char* pos = list_bsearch(pItem, LIST_START(pList), GetNumItems(pList), pList->dwSize, cmpproc2, (void**)&newpos);
    if (pos == NULL) {
        pos = newpos;
    }
    memmove(pos + pList->dwSize, pos, (char*)pList->pFree - pos);
    pList->pFree = (char*)pList->pFree + pList->dwSize;
    ((struct NAMEITEM*)pos)->pszName = pItem;
    return pos;
}

// add an array of - already sorted! - items to a list

void* AddItemArrayList(struct LIST* pList, struct NAMEITEM *pItems, uint32_t dwNum) {
    char* newPFree = (char*)pList->pFree + dwNum * pList->dwSize;
    if (newPFree > (char*)pList->pFree) {
        return NULL;
    }
    memcpy(pList->pFree, pItems, sizeof(struct NAMEITEM) * dwNum);
    char* prevPFree = pList->pFree;
    pList->pFree = newPFree;
    return prevPFree;
}

// get the next item in a list

void* GetNextItemList(struct LIST* pList, struct NAMEITEM* pPrevItem) {
    void* res;
    if (pPrevItem == NULL) {
        res = LIST_START(pList);
    } else {
        res = (char*)pPrevItem + sizeof(struct NAMEITEM);
    }
    if (res >= pList->pFree) {
        return NULL;
    }
    return res;
}

// find an item in a list

void* FindItemList(struct LIST* pList, char* pszName) {
    void* pNewItem;
    return list_bsearch((struct NAMEITEM*)pszName, LIST_START(pList), GetNumItems(pList), pList->dwSize, cmpproc2, &pNewItem);
}

int cmpproc(const void* p1, const void* p2) {
    const char*const * s1 = p1;
    const char*const* s2 = p2;

    return stricmp(*s1, *s2);
}

void SortList(struct LIST* pList) {
    qsort(LIST_START(pList), GetNumItems(pList), pList->dwSize, cmpproc);
}

// sort case sensitive

void SortCSList(struct LIST* pList) {
    qsort(LIST_START(pList), GetNumItems(pList), pList->dwSize, cmpproc2);
}

uint32_t GetItemSizeList(const struct LIST* pList) {
    return pList->dwSize;
}

uint32_t GetNumItemsList(const struct LIST* pList) {
    return GetNumItems(pList);
}

void PrintList(const struct LIST *pList) {
    for (uint32_t i = 0; i < GetNumItems(pList); i++) {
        printf("%d -> %s\n", i, ((struct NAMEITEM*)(((char*)&pList[1]) + i * pList->dwSize))->pszName);
    }
}
