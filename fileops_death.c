// a module which, when it's called, reports and kills itself.

#include "nv_common.h"

BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _death_, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _death_OPEN(INTF_OPEN);
RETT_IOCTL _death_IOCTL(INTF_IOCTL);

int _death_resolver(char* tree) { return 0; }

void _death_init2(void)
{
	_hub_find_fileop("death")->resolve = _death_resolver;
}

MODULE_REGISTRATION_F("death", _death_, _death_init2(); );

#define DEATH_FUNCT(FUNCT) \
	RETT_##FUNCT _death_##FUNCT(INTF_##FUNCT) { \
		DEBUG("_death_" #FUNCT " called.  Goodbye cruel world!\n"); \
		assert(0); \
		return (RETT_##FUNCT)-1; \
	}

#define DEATH_FUNCT_IWRAP(r, data, elem) DEATH_FUNCT(elem)

BOOST_PP_SEQ_FOR_EACH(DEATH_FUNCT_IWRAP, x, ALLOPS_WPAREN)

