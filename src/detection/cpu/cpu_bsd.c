#include "cpu.h"
#include "common/sysctl.h"
#include "detection/temps/temps_bsd.h"

const char* ffDetectCPUImpl(const FFCPUOptions* options, FFCPUResult* cpu)
{
    if (ffSysctlGetString("hw.model", &cpu->name))
        return "sysctlbyname(hw.model) failed";

    cpu->coresPhysical = (uint16_t) ffSysctlGetInt("hw.ncpu", 1);
    cpu->coresLogical = cpu->coresPhysical;
    cpu->coresOnline = cpu->coresPhysical;

    cpu->frequencyMin = ffSysctlGetInt("hw.clockrate", 0) / 1000.0;
    cpu->frequencyMax = cpu->frequencyMin;
    cpu->temperature = FF_CPU_TEMP_UNSET;

    if (options->temp)
        ffDetectThermalTemp(&cpu->temperature);

    return NULL;
}
