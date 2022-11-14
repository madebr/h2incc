// driver: args=-C %INICONFIG%
// driver: expected=success
// driver: reference=struct_member_protected_word.ref
struct name {
    int align;
    int eax;
    int ebx;
};

struct name2 {
    int align;
    int aligned;
    int eax;
    int r00;
    int ebx;
    ine ecx3;
};
