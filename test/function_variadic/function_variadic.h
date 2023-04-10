// driver: args=
// driver: expected=success
// driver: reference=function_variadic.ref
#include <stdarg.h>
void fatal(char *name, int line, char *s, ...);
