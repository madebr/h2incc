// driver: args=
// driver: expected=success
// driver: reference=enum_anon_val.ref
enum {
    VALUE_A = 0x00004000,
#if 0
    VALUE_B = 0x00008000,
#endif
    VALUE_C	= 0x00010000,
};
