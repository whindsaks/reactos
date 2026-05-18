
#define STANDALONE
#include <wine/test.h>

extern void func_ntoskrnl_SectionFlags(void);
extern void func_delayload(void);

const struct test winetest_testlist[] =
{
    { "ntoskrnl_SectionFlags",                  func_ntoskrnl_SectionFlags },
    { "delayload",                              func_delayload },

    { 0, 0 }
};

