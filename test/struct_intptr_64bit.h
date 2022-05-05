// driver: args=-x
// driver: expected=success
// driver: reference=struct_intptr_64bit.ref
struct somestruct {
    int* a;
    int** b;
    int*** c;
};
