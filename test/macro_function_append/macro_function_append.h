// driver: args=
// driver: expected=success
// driver: reference=macro_function_append.ref
#define MACRO_FUNC(a,b,c,d) (ARG_##a) | (ARG_##b) | (ARG_##c) | (ARG_##d)
enum {
    BR_EULER_XYZ_S = MACRO_FUNC(1, 2, 3, 4),
}
