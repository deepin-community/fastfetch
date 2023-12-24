#include "common/printing.h"
#include "common/jsonconfig.h"
#include "modules/title/title.h"
#include "util/textModifier.h"
#include "util/stringUtils.h"

#define FF_TITLE_NUM_FORMAT_ARGS 8

static void appendText(FFstrbuf* output, const FFstrbuf* text, const FFstrbuf* color)
{
    if (!instance.config.display.pipe)
    {
        if (instance.config.display.brightColor)
            ffStrbufAppendS(output, FASTFETCH_TEXT_MODIFIER_BOLT);
        if (color->length > 0)
            ffStrbufAppendF(output, "\e[%sm", color->chars);
        else if (instance.config.display.colorTitle.length > 0)
            ffStrbufAppendF(output, "\e[%sm", instance.config.display.colorTitle.chars);
    }

    ffStrbufAppend(output, text);

    if(!instance.config.display.pipe)
        ffStrbufAppendS(output, FASTFETCH_TEXT_MODIFIER_RESET);
}

void ffPrintTitle(FFTitleOptions* options)
{
    FF_STRBUF_AUTO_DESTROY userNameColored = ffStrbufCreate();
    appendText(&userNameColored, &instance.state.platform.userName, &options->colorUser);

    FF_STRBUF_AUTO_DESTROY hostName = ffStrbufCreateCopy(&instance.state.platform.hostName);
    if (!options->fqdn)
        ffStrbufSubstrBeforeFirstC(&hostName, '.');

    FF_STRBUF_AUTO_DESTROY hostNameColored = ffStrbufCreate();
    appendText(&hostNameColored, &hostName, &options->colorHost);

    FF_STRBUF_AUTO_DESTROY atColored = ffStrbufCreate();
    if (!instance.config.display.pipe && options->colorAt.length > 0)
    {
        ffStrbufAppendF(&atColored, "\e[%sm", options->colorAt.chars);
        ffStrbufAppendC(&atColored, '@');
        ffStrbufAppendS(&atColored, FASTFETCH_TEXT_MODIFIER_RESET);
    }
    else
        ffStrbufAppendC(&atColored, '@');

    if (options->moduleArgs.outputFormat.length == 0)
    {
        ffPrintLogoAndKey(FF_TITLE_MODULE_NAME, 0, &options->moduleArgs, FF_PRINT_TYPE_DEFAULT);

        ffStrbufWriteTo(&userNameColored, stdout);
        ffStrbufWriteTo(&atColored, stdout);
        ffStrbufPutTo(&hostNameColored, stdout);
    }
    else
    {
        ffPrintFormat(FF_TITLE_MODULE_NAME, 0, &options->moduleArgs, FF_TITLE_NUM_FORMAT_ARGS, (FFformatarg[]){
            {FF_FORMAT_ARG_TYPE_STRBUF, &instance.state.platform.userName},
            {FF_FORMAT_ARG_TYPE_STRBUF, &hostName},
            {FF_FORMAT_ARG_TYPE_STRBUF, &instance.state.platform.homeDir},
            {FF_FORMAT_ARG_TYPE_STRBUF, &instance.state.platform.exePath},
            {FF_FORMAT_ARG_TYPE_STRBUF, &instance.state.platform.userShell},
            {FF_FORMAT_ARG_TYPE_STRBUF, &userNameColored},
            {FF_FORMAT_ARG_TYPE_STRBUF, &atColored},
            {FF_FORMAT_ARG_TYPE_STRBUF, &hostNameColored},
        });
    }
}

bool ffParseTitleCommandOptions(FFTitleOptions* options, const char* key, const char* value)
{
    const char* subKey = ffOptionTestPrefix(key, FF_TITLE_MODULE_NAME);
    if (!subKey) return false;
    if (ffOptionParseModuleArgs(key, subKey, value, &options->moduleArgs))
        return true;

    if (ffStrEqualsIgnCase(subKey, "fqdn"))
    {
        options->fqdn = ffOptionParseBoolean(value);
        return true;
    }

    if (ffStrEqualsIgnCase(subKey, "color-user"))
    {
        ffOptionParseColor(value, &options->colorUser);
        return true;
    }

    if (ffStrEqualsIgnCase(subKey, "color-at"))
    {
        ffOptionParseColor(value, &options->colorAt);
        return true;
    }

    if (ffStrEqualsIgnCase(subKey, "color-host"))
    {
        ffOptionParseColor(value, &options->colorHost);
        return true;
    }

    return false;
}

void ffParseTitleJsonObject(FFTitleOptions* options, yyjson_val* module)
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

        if (ffStrEqualsIgnCase(key, "fqdn"))
        {
            options->fqdn = yyjson_get_bool(val);
            continue;
        }

        if (ffStrEqualsIgnCase(key, "color"))
        {
            if (!yyjson_is_obj(val))
                continue;

            yyjson_val* color = yyjson_obj_get(val, "user");
            if (color)
                ffOptionParseColor(yyjson_get_str(color), &options->colorUser);
            color = yyjson_obj_get(val, "at");
            if (color)
                ffOptionParseColor(yyjson_get_str(color), &options->colorAt);
            color = yyjson_obj_get(val, "host");
            if (color)
                ffOptionParseColor(yyjson_get_str(color), &options->colorHost);
            continue;
        }

        ffPrintErrorString(FF_TITLE_MODULE_NAME, 0, NULL, FF_PRINT_TYPE_NO_CUSTOM_KEY, "Unknown JSON key %s", key);
    }
}

void ffGenerateTitleJsonConfig(FFTitleOptions* options, yyjson_mut_doc* doc, yyjson_mut_val* module)
{
    __attribute__((__cleanup__(ffDestroyTitleOptions))) FFTitleOptions defaultOptions;
    ffInitTitleOptions(&defaultOptions);

    ffJsonConfigGenerateModuleArgsConfig(doc, module, &defaultOptions.moduleArgs, &options->moduleArgs);

    if (defaultOptions.fqdn != options->fqdn)
        yyjson_mut_obj_add_bool(doc, module, "fqdn", options->fqdn);

    yyjson_mut_val* color = yyjson_mut_obj(doc);

    if (!ffStrbufEqual(&options->colorUser, &defaultOptions.colorUser))
        yyjson_mut_obj_add_strbuf(doc, color, "user", &options->colorUser);

    if (!ffStrbufEqual(&options->colorAt, &defaultOptions.colorAt))
        yyjson_mut_obj_add_strbuf(doc, color, "at", &options->colorAt);

    if (!ffStrbufEqual(&options->colorHost, &defaultOptions.colorHost))
        yyjson_mut_obj_add_strbuf(doc, color, "host", &options->colorHost);

    if (yyjson_mut_obj_size(color))
        yyjson_mut_obj_add_val(doc, module, "color", color);
}

void ffGenerateTitleJsonResult(FF_MAYBE_UNUSED FFTitleOptions* options, yyjson_mut_doc* doc, yyjson_mut_val* module)
{
    yyjson_mut_val* obj = yyjson_mut_obj_add_obj(doc, module, "result");
    yyjson_mut_obj_add_strbuf(doc, obj, "userName", &instance.state.platform.userName);
    yyjson_mut_obj_add_strbuf(doc, obj, "hostName", &instance.state.platform.hostName);
    yyjson_mut_obj_add_strbuf(doc, obj, "homeDir", &instance.state.platform.homeDir);
    yyjson_mut_obj_add_strbuf(doc, obj, "exePath", &instance.state.platform.exePath);
    yyjson_mut_obj_add_strbuf(doc, obj, "userShell", &instance.state.platform.userShell);
    yyjson_mut_obj_add_uint(doc, obj, "pageSize", instance.state.platform.pageSize);
}

void ffPrintTitleHelpFormat(void)
{
    ffPrintModuleFormatHelp(FF_TITLE_MODULE_NAME, "{6}{7}{8}", FF_TITLE_NUM_FORMAT_ARGS, (const char* []) {
        "User name",
        "Host name",
        "Home directory",
        "Executable path of current process",
        "User's default shell",
        "User name (colored)",
        "@ symbol (colored)",
        "Host name (colored)"
    });
}

void ffInitTitleOptions(FFTitleOptions* options)
{
    ffOptionInitModuleBaseInfo(
        &options->moduleInfo,
        FF_TITLE_MODULE_NAME,
        "Print title, which contains your user name, hostname",
        ffParseTitleCommandOptions,
        ffParseTitleJsonObject,
        ffPrintTitle,
        ffGenerateTitleJsonResult,
        ffPrintTitleHelpFormat,
        ffGenerateTitleJsonConfig
    );
    ffOptionInitModuleArg(&options->moduleArgs);
    ffStrbufSetStatic(&options->moduleArgs.key, " ");

    options->fqdn = false;
    ffStrbufInit(&options->colorUser);
    ffStrbufInit(&options->colorAt);
    ffStrbufInit(&options->colorHost);
}

void ffDestroyTitleOptions(FFTitleOptions* options)
{
    ffOptionDestroyModuleArg(&options->moduleArgs);
    ffStrbufDestroy(&options->colorUser);
    ffStrbufDestroy(&options->colorAt);
    ffStrbufDestroy(&options->colorHost);
}
