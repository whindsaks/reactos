
#define STANDALONE
#include <wine/test.h>

extern void func_initializespy(void);
extern void func_propvariant(void);

const struct test winetest_testlist[] =
{
    { "initializespy", func_initializespy },
    { "propvariant", func_propvariant },

    { 0, 0 }
};
