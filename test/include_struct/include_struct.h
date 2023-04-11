// driver: args=
// driver: expected=success
// driver: reference=include_struct.ref

#include "local.h"

struct user {
    int e1;
    subtype1 e2;
    subtype2 e3[3];
    short e4;
};

extern subtype1 s1;
extern subtype2 s2;
