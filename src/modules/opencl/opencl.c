#include "common/printing.h"
#include "common/jsonconfig.h"
#include "detection/opencl/opencl.h"
#include "modules/opencl/opencl.h"
#include "util/stringUtils.h"

#define FF_OPENCL_NUM_FORMAT_ARGS 3

void ffPrintOpenCL(FFOpenCLOptions* options)
{
    FFOpenCLResult opencl;
    ffStrbufInit(&opencl.version);
    ffStrbufInit(&opencl.device);
    ffStrbufInit(&opencl.vendor);

    const char* error = ffDetectOpenCL(&opencl);

    if(error != NULL)
        ffPrintError(FF_OPENCL_MODULE_NAME, 0, &options->moduleArgs, "%s", error);
    else
    {
        if(options->moduleArgs.outputFormat.length == 0)
        {
            ffPrintLogoAndKey(FF_OPENCL_MODULE_NAME, 0, &options->moduleArgs, FF_PRINT_TYPE_DEFAULT);
            ffStrbufPutTo(&opencl.version, stdout);
        }
        else
        {
            ffPrintFormat(FF_OPENCL_MODULE_NAME, 0, &options->moduleArgs, FF_OPENCL_NUM_FORMAT_ARGS, (FFformatarg[]) {
                {FF_FORMAT_ARG_TYPE_STRBUF, &opencl.version},
                {FF_FORMAT_ARG_TYPE_STRBUF, &opencl.device},
                {FF_FORMAT_ARG_TYPE_STRBUF, &opencl.vendor},
            });
        }
    }

    ffStrbufDestroy(&opencl.version);
    ffStrbufDestroy(&opencl.device);
    ffStrbufDestroy(&opencl.vendor);
}

void ffInitOpenCLOptions(FFOpenCLOptions* options)
{
    ffOptionInitModuleBaseInfo(&options->moduleInfo, FF_OPENCL_MODULE_NAME, ffParseOpenCLCommandOptions, ffParseOpenCLJsonObject, ffPrintOpenCL);
    ffOptionInitModuleArg(&options->moduleArgs);
}

bool ffParseOpenCLCommandOptions(FFOpenCLOptions* options, const char* key, const char* value)
{
    const char* subKey = ffOptionTestPrefix(key, FF_OPENCL_MODULE_NAME);
    if (!subKey) return false;
    if (ffOptionParseModuleArgs(key, subKey, value, &options->moduleArgs))
        return true;

    return false;
}

void ffDestroyOpenCLOptions(FFOpenCLOptions* options)
{
    ffOptionDestroyModuleArg(&options->moduleArgs);
}

void ffParseOpenCLJsonObject(FFOpenCLOptions* options, yyjson_val* module)
{
    yyjson_val *key_, *val;
    size_t idx, max;
    yyjson_obj_foreach(module, idx, max, key_, val)
    {
        const char* key = yyjson_get_str(key_);
        if(ffStrEqualsIgnCase(key, "type"))
            continue;

        if (ffJsonConfigParseModuleArgs(key, val, &options->moduleArgs))
            continue;

        ffPrintError(FF_OPENCL_MODULE_NAME, 0, &options->moduleArgs, "Unknown JSON key %s", key);
    }
}
