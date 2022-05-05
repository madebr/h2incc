// driver: args=
// driver: expected=success
// driver: reference=union_simple.h.ref
union some_union {
    int a;
    float b;
    double c;
    struct {
        int a;
        int b;
    } s;
};
