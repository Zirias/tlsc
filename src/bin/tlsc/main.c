#include "config.h"
#include "tlsc.h"

int main(int argc, char **argv)
{
    Config cfg;
    Config_fromOpts(&cfg, argc, argv);

    return Tlsc_run(&cfg);
}

