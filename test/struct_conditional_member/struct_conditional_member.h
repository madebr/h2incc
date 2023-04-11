// driver: args=
// driver: expected=success
// driver: reference=struct_conditional_member.ref
#ifndef GUARD_H
#define GUARD_H

#define CONDITIONAL 0

typedef struct my_struct {
    int member1;
#if CONDITIONAL
    int conditional_member1;
    int conditional_member2;
#endif
} my_struct;

#endif
