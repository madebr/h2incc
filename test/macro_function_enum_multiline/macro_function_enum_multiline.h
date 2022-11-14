// driver: args=
// driver: expected=success
// driver: reference=macro_function_enum_multiline.ref
#define SOME_FUNC(A, B, C, D) \
	((A + B) + \
    (C + D))

enum {
    A = SOME_FUNC(1, 2, 3, 4),
    B = SOME_FUNC(2, 3, 4, 5),
    C = SOME_FUNC(3, 4, 5, 6),
};
