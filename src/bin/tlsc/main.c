#include "config.h"
#include "tlsc.h"

#include <stdlib.h>

int main(int argc, char **argv)
{
    int rc = EXIT_FAILURE;
    Config *cfg = Config_fromOpts(argc, argv);
    if (cfg)
    {
	rc = Tlsc_run(cfg);
	Config_destroy(cfg);
    }
    return rc;
}

