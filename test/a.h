struct something;
#define BR_ASM_DATA
#ifdef __cplusplus
extern "C" {
#endif
extern struct something * BR_ASM_DATA SomeGlobalVariable1;
extern struct something * BR_ASM_DATA SomeGlobalVariable2;
struct S {
    int a;
};
//#ifdef __cplusplus
//};
//#endif
