// driver: args=-C %INICONFIG%
// driver: expected=success
// driver: reference=extern_funcptr.h.ref
extern int (*some_func)(int arg1, void* arg2);
