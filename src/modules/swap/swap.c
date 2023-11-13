#include "common/printing.h"
#include "common/jsonconfig.h"
#include "common/parsing.h"
#include "common/bar.h"
#include "detection/swap/swap.h"
#include "modules/swap/swap.h"
#include "util/stringUtils.h"

#define FF_SWAP_NUM_FORMAT_ARGS 3

void ffPrintSwap(FFSwapOptions* options)
{
    FFSwapResult storage;
    const char* error = ffDetectSwap(&storage);

    if(error)
    {
        ffPrintError(FF_SWAP_MODULE_NAME, 0, &options->moduleArgs, "%s", error);
        return;
    }

    FF_STRBUF_AUTO_DESTROY usedPretty = ffStrbufCreate();
    ffParseSize(storage.bytesUsed, &usedPretty);

    FF_STRBUF_AUTO_DESTROY totalPretty = ffStrbufCreate();
    ffParseSize(storage.bytesTotal, &totalPretty);

    double percentage = storage.bytesTotal == 0
        ? 0
        : (double) storage.bytesUsed / (double) storage.bytesTotal * 100.0;

    if(options->moduleArgs.outputFormat.length == 0)
    {
        ffPrintLogoAndKey(FF_SWAP_MODULE_NAME, 0, &options->moduleArgs, FF_PRINT_TYPE_DEFAULT);
        FF_STRBUF_AUTO_DESTROY str = ffStrbufCreate();
        if (storage.bytesTotal == 0)
        {
            if(instance.config.display.percentType & FF_PERCENTAGE_TYPE_BAR_BIT)
            {
                ffAppendPercentBar(&str, 0, 0, 50, 80);
                ffStrbufAppendC(&str, ' ');
            }
            if(!(instance.config.display.percentType & FF_PERCENTAGE_TYPE_HIDE_OTHERS_BIT))
                ffStrbufAppendS(&str, "Disabled");
        }
        else
        {
            if(instance.config.display.percentType & FF_PERCENTAGE_TYPE_BAR_BIT)
            {
                ffAppendPercentBar(&str, percentage, 0, 50, 80);
                ffStrbufAppendC(&str, ' ');
            }

            if(!(instance.config.display.percentType & FF_PERCENTAGE_TYPE_HIDE_OTHERS_BIT))
                ffStrbufAppendF(&str, "%s / %s ", usedPretty.chars, totalPretty.chars);

            if(instance.config.display.percentType & FF_PERCENTAGE_TYPE_NUM_BIT)
                ffAppendPercentNum(&str, percentage, 50, 80, str.length > 0);
        }

        ffStrbufTrimRight(&str, ' ');
        ffStrbufPutTo(&str, stdout);
    }
    else
    {
        FF_STRBUF_AUTO_DESTROY percentageStr = ffStrbufCreate();
        ffAppendPercentNum(&percentageStr, percentage, 50, 80, false);
        ffPrintFormat(FF_SWAP_MODULE_NAME, 0, &options->moduleArgs, FF_SWAP_NUM_FORMAT_ARGS, (FFformatarg[]){
            {FF_FORMAT_ARG_TYPE_STRBUF, &usedPretty},
            {FF_FORMAT_ARG_TYPE_STRBUF, &totalPretty},
            {FF_FORMAT_ARG_TYPE_STRBUF, &percentageStr},
        });
    }
}

bool ffParseSwapCommandOptions(FFSwapOptions* options, const char* key, const char* value)
{
    const char* subKey = ffOptionTestPrefix(key, FF_SWAP_MODULE_NAME);
    if (!subKey) return false;
    if (ffOptionParseModuleArgs(key, subKey, value, &options->moduleArgs))
        return true;

    return false;
}

void ffParseSwapJsonObject(FFSwapOptions* options, yyjson_val* module)
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

        ffPrintError(FF_SWAP_MODULE_NAME, 0, &options->moduleArgs, "Unknown JSON key %s", key);
    }
}

void ffGenerateSwapJsonConfig(FFSwapOptions* options, yyjson_mut_doc* doc, yyjson_mut_val* module)
{
    __attribute__((__cleanup__(ffDestroySwapOptions))) FFSwapOptions defaultOptions;
    ffInitSwapOptions(&defaultOptions);

    ffJsonConfigGenerateModuleArgsConfig(doc, module, &defaultOptions.moduleArgs, &options->moduleArgs);
}

void ffGenerateSwapJsonResult(FF_MAYBE_UNUSED FFSwapOptions* options, yyjson_mut_doc* doc, yyjson_mut_val* module)
{
    FFSwapResult storage;
    const char* error = ffDetectSwap(&storage);

    if(error)
    {
        yyjson_mut_obj_add_str(doc, module, "error", error);
        return;
    }

    yyjson_mut_val* obj = yyjson_mut_obj_add_obj(doc, module, "result");
    yyjson_mut_obj_add_uint(doc, obj, "total", storage.bytesTotal);
    yyjson_mut_obj_add_uint(doc, obj, "used", storage.bytesUsed);
}

void ffPrintSwapHelpFormat(void)
{
    ffPrintModuleFormatHelp(FF_SWAP_MODULE_NAME, "{1} / {2} ({3})", FF_SWAP_NUM_FORMAT_ARGS, (const char* []) {
        "Used size",
        "Total size",
        "Percentage used"
    });
}

void ffInitSwapOptions(FFSwapOptions* options)
{
    ffOptionInitModuleBaseInfo(
        &options->moduleInfo,
        FF_SWAP_MODULE_NAME,
        ffParseSwapCommandOptions,
        ffParseSwapJsonObject,
        ffPrintSwap,
        ffGenerateSwapJsonResult,
        ffPrintSwapHelpFormat,
        ffGenerateSwapJsonConfig
    );
    ffOptionInitModuleArg(&options->moduleArgs);
}

void ffDestroySwapOptions(FFSwapOptions* options)
{
    ffOptionDestroyModuleArg(&options->moduleArgs);
}
