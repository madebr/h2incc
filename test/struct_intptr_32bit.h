// driver: args=
// driver: expected=success
// driver: reference=struct_intptr_32bit.ref
struct somestruct {
    int* a;
    int** b;
    int*** c;
};
