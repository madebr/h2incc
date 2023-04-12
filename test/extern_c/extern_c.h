// driver: args=
// driver: expected=success
// driver: reference=extern_c.ref
struct something;
#define SOME_MACRO
#ifdef __cplusplus
extern struct something * SOME_MACRO SomeGlobalVariable1;
extern struct something * SOME_MACRO SomeGlobalVariable2;
extern "C" {
#endif
extern struct something * SOME_MACRO SomeGlobalVariable3;
extern struct something * SOME_MACRO SomeGlobalVariable4;
#ifdef __cplusplus
};
#endif
extern struct something * SOME_MACRO SomeGlobalVariable5;
extern struct something * SOME_MACRO SomeGlobalVariable6;
