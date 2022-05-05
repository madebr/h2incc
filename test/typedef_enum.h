// driver: args=
// driver: expected=success
// driver: reference=typedef_enum.h.ref
typedef enum some_enum_t {
    A = 0,
    B = 1,
    C = '{',
    END = 5
} some_enum_t;
