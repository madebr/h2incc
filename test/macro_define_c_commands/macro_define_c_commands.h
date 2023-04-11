// driver: args=
// driver: expected=success
// driver: reference=macro_define_c_commands.ref

#define CopyVector(v1, v2) do { \
	(v1)->v[0] = (v2)->v[0];      \
	(v1)->v[1] = (v2)->v[1];      \
} while(0)
