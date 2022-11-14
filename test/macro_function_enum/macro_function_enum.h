// driver: args=
// driver: expected=success
// driver: reference=macro_function_enum.ref

#define MACRO_FUNC(A, B) (A + B)

enum {
    A = MACRO_FUNC(1, 2),
    B = MACRO_FUNC(2, 3),
    C = MACRO_FUNC(3, 4),
};
