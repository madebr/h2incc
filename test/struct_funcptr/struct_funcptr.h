// driver: args=
// driver: expected=success
// driver: reference=struct_funcptr.ref
typedef struct s_abc {
    int (*member)(char arg1, int arg2, char* arg3, void* arg4);
} abc_t;
