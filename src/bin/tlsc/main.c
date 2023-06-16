#include "config.h"
#include "tlsc.h"

#include <stdlib.h>

int main(int argc, char **argv)
{
    int rc = EXIT_FAILURE;
    PSC_Config *cfg = Config_fromOpts(argc, argv);
    if (cfg)
    {
	rc = Tlsc_run(cfg);
	PSC_Config_destroy(cfg);
    }
    return rc;
}

