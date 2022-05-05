// driver: args=
// driver: expected=success
// driver: reference=struct_typedef.h.ref
typedef struct s_abc{
    struct s_abc* next;
    int a;
} abc_t;

extern struct s_abc* item1;
extern abc_t* item2;

extern struct s_abc item3;
extern abc_t item4;
