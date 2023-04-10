// driver: args=
// driver: expected=success
// driver: reference=macro_function_append.ref
#define MACRO_FUNC(a,b,c,d,e) (ARG_##a) | (ARG_##b) | (ARG_##c) | (ARG_##d) | (ARG_##e)
enum {
    BR_EULER_XYZ_S = MACRO_FUNC(1, 2, 3, 4, 5),
}
