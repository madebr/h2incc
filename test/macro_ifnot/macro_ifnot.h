// driver: args=
// driver: expected=success
// driver: reference=macro_ifnot.ref

#if !OPTION_A && !OPTION_B
#error "Must define OPTION_A or OPTION_B"
#endif
