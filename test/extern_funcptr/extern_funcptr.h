// driver: args=-C %INICONFIG%
// driver: expected=success
// driver: reference=extern_funcptr.ref
extern int (*some_func)(int arg1, void* arg2);
