// driver: args=
// driver: expected=success
// driver: reference=macro_ifdef.ref
struct abc {
#ifdef ABC
int a;
#elif defined(DEF)
int b;
#else
int c;
#endif
};
