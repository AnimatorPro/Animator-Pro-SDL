#include <pocolib.h>
#include <pocorex.h>

static int legacy_poe_answer(void)
{
	return 73;
}

static Lib_proto legacy_poe_bindings[] = {
	{legacy_poe_answer, "int LegacyPoeAnswer(void);"},
};

Setup_Pocorex(NULL, NULL, "legacy-poe-fence", legacy_poe_bindings);
