// driver: args=
// driver: expected=success
// driver: reference=typedef_function.ref
typedef int (*func_t)(int a, double b);

int my_sort(void* a, void* b, func_t* cmp);
