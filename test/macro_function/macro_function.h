// driver: args=
// driver: expected=success
// driver: reference=macro_function.ref
#define MACRO_FUNC(A)   (A)
#define MACRO_FUNC2(A,B)  ((A)+(B))


#define S  MACRO_FUNC(4)
#define T  MACRO_FUNC2(1,2)
