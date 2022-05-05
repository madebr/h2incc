// driver: args=
// driver: expected=success
// driver: reference=struct_embedded.h.ref
struct struct1 {
    struct {
        int a;
        int b;
    } s1;
    struct {
        int c;
        int d;
    } s2;
};
