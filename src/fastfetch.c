#include "fastfetch.h"
#include "common/commandoption.h"
#include "common/printing.h"
#include "common/parsing.h"
#include "common/io/io.h"
#include "common/time.h"
#include "common/jsonconfig.h"
#include "detection/version/version.h"
#include "util/stringUtils.h"
#include "logo/logo.h"
#include "fastfetch_datatext.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <inttypes.h>

#ifdef WIN32
    #include "util/windows/getline.h"
#endif

#include "modules/modules.h"

static void printCommandFormatHelp(const char* command)
{
    FF_STRBUF_AUTO_DESTROY type = ffStrbufCreateNS((uint32_t) (strlen(command) - strlen("-format")), command);
    for (FFModuleBaseInfo** modules = ffModuleInfos[toupper(command[0]) - 'A']; *modules; ++modules)
    {
        FFModuleBaseInfo* baseInfo = *modules;
        if (ffStrbufIgnCaseEqualS(&type, baseInfo->name))
        {
            if (baseInfo->printHelpFormat)
                baseInfo->printHelpFormat();
            else
                fprintf(stderr, "Error: Module '%s' doesn't support output formatting\n", baseInfo->name);
            return;
        }
    }

    fprintf(stderr, "Error: Module '%s' is not supported\n", type.chars);
}

static inline void printCommandHelp(const char* command)
{
    if(command == NULL)
        puts(FASTFETCH_DATATEXT_HELP);
    else if(ffStrEqualsIgnCase(command, "color"))
        puts(FASTFETCH_DATATEXT_HELP_COLOR);
    else if(ffStrEqualsIgnCase(command, "format"))
        puts(FASTFETCH_DATATEXT_HELP_FORMAT);
    else if(ffStrEqualsIgnCase(command, "load-config") || ffStrEqualsIgnCase(command, "config"))
        puts(FASTFETCH_DATATEXT_HELP_CONFIG);
    else if(isalpha(command[0]) && ffStrEndsWithIgnCase(command, "-format")) // x-format
        printCommandFormatHelp(command);
    else
        fprintf(stderr, "Error: No specific help for command '%s' provided\n", command);
}

static void listAvailablePresets(void)
{
    FF_LIST_FOR_EACH(FFstrbuf, path, instance.state.platform.dataDirs)
    {
        ffStrbufAppendS(path, "fastfetch/presets/");
        ffListFilesRecursively(path->chars);
    }
}

static void listAvailableLogos(void)
{
    FF_LIST_FOR_EACH(FFstrbuf, path, instance.state.platform.dataDirs)
    {
        ffStrbufAppendS(path, "fastfetch/logos/");
        ffListFilesRecursively(path->chars);
    }
}

static void listConfigPaths(void)
{
    FF_LIST_FOR_EACH(FFstrbuf, folder, instance.state.platform.configDirs)
    {
        bool exists = false;
        ffStrbufAppendS(folder, "fastfetch/config.jsonc");
        exists = ffPathExists(folder->chars, FF_PATHTYPE_FILE);
        if (!exists)
        {
            ffStrbufSubstrBefore(folder, folder->length - (uint32_t) strlen("jsonc"));
            ffStrbufAppendS(folder, "conf");
            exists = ffPathExists(folder->chars, FF_PATHTYPE_FILE);
        }
        printf("%s%s\n", folder->chars, exists ? " (*)" : "");
    }
}

static void listDataPaths(void)
{
    FF_LIST_FOR_EACH(FFstrbuf, folder, instance.state.platform.dataDirs)
    {
        ffStrbufAppendS(folder, "fastfetch/");
        puts(folder->chars);
    }
}

static void listModules()
{
    unsigned count = 0;
    for (int i = 0; i <= 'Z' - 'A'; ++i)
    {
        for (FFModuleBaseInfo** modules = ffModuleInfos[i]; *modules; ++modules)
        {
            ++count;
            printf("%d)%s%s\n", count, count > 9 ? " " : "  ", (*modules)->name);
        }
    }
}

static void parseOption(FFdata* data, const char* key, const char* value);

static bool parseJsoncFile(const char* path)
{
    yyjson_read_err error;
    yyjson_doc* doc = yyjson_read_file(path, YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_INF_AND_NAN, NULL, &error);
    if (!doc)
    {
        if (error.code != YYJSON_READ_ERROR_FILE_OPEN)
        {
            fprintf(stderr, "Error: failed to parse JSON config file `%s` at pos %zu: %s\n", path, error.pos, error.msg);
            exit(477);
        }
        return false;
    }
    if (instance.state.configDoc)
        yyjson_doc_free(instance.state.configDoc); // for `--load-config`

    instance.state.configDoc = doc;
    return true;
}

static bool parseConfigFile(FFdata* data, const char* path)
{
    FILE* file = fopen(path, "r");
    if(file == NULL)
        return false;

    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    FF_STRBUF_AUTO_DESTROY unescaped = ffStrbufCreate();

    while ((read = getline(&line, &len, file)) != -1)
    {
        char* lineStart = line;
        char* lineEnd = line + read - 1;

        //Trim line left
        while(isspace(*lineStart))
            ++lineStart;

        //Continue if line is empty or a comment
        if(*lineStart == '\0' || *lineStart == '#')
            continue;

        //Trim line right
        while(lineEnd > lineStart && isspace(*lineEnd))
            --lineEnd;
        *(lineEnd + 1) = '\0';

        char* valueStart = strchr(lineStart, ' ');

        //If the line has no white space, it is only a key
        if(valueStart == NULL)
        {
            parseOption(data, lineStart, NULL);
            continue;
        }

        //separate the key from the value
        *valueStart = '\0';
        ++valueStart;

        //Trim space of value left
        while(isspace(*valueStart))
            ++valueStart;

        //If we want whitespace in values, we need to quote it. This is done to keep consistency with shell.
        if((*valueStart == '"' || *valueStart == '\'') && *valueStart == *lineEnd && lineEnd > valueStart)
        {
            ++valueStart;
            *lineEnd = '\0';
            --lineEnd;
        }

        if (strchr(valueStart, '\\'))
        {
            // Unescape all `\x`s
            const char* value = valueStart;
            while(*value != '\0')
            {
                if(*value != '\\')
                {
                    ffStrbufAppendC(&unescaped, *value);
                    ++value;
                    continue;
                }

                ++value;

                switch(*value)
                {
                    case 'n': ffStrbufAppendC(&unescaped, '\n'); break;
                    case 't': ffStrbufAppendC(&unescaped, '\t'); break;
                    case 'e': ffStrbufAppendC(&unescaped, '\e'); break;
                    case '\\': ffStrbufAppendC(&unescaped, '\\'); break;
                    default:
                        ffStrbufAppendC(&unescaped, '\\');
                        ffStrbufAppendC(&unescaped, *value);
                        break;
                }

                ++value;
            }
            parseOption(data, lineStart, unescaped.chars);
            ffStrbufClear(&unescaped);
        }
        else
        {
            parseOption(data, lineStart, valueStart);
        }
    }

    if(line != NULL)
        free(line);

    fclose(file);
    return true;
}

static void generateConfigFile(bool force, const char* type)
{
    FFstrbuf* filename = (FFstrbuf*) ffListGet(&instance.state.platform.configDirs, 0);
    // Paths generated in `init.c/initConfigDirs` end with `/`
    bool isJsonc = false;
    if (type)
    {
        if (ffStrEqualsIgnCase(type, "jsonc"))
            isJsonc = true;
        else if (!ffStrEqualsIgnCase(type, "conf"))
        {
            fputs("config type can only be `jsonc` or `conf`\n", stderr);
            exit(1);
        }
    }

    ffStrbufAppendS(filename, isJsonc ? "fastfetch/config.jsonc" : "fastfetch/config.conf");

    if (!force && ffPathExists(filename->chars, FF_PATHTYPE_FILE))
    {
        fprintf(stderr, "Config file exists in `%s`, use `--gen-config-force` to overwrite\n", filename->chars);
        exit(1);
    }
    else
    {
        ffWriteFileData(
            filename->chars,
            isJsonc ? strlen(FASTFETCH_DATATEXT_CONFIG_USER_JSONC) : strlen(FASTFETCH_DATATEXT_CONFIG_USER),
            isJsonc ? FASTFETCH_DATATEXT_CONFIG_USER_JSONC : FASTFETCH_DATATEXT_CONFIG_USER);
        printf("A sample config file has been written in `%s`\n", filename->chars);
        exit(0);
    }
}

static void optionParseConfigFile(FFdata* data, const char* key, const char* value)
{
    if(value == NULL)
    {
        fprintf(stderr, "Error: usage: %s <file>\n", key);
        exit(413);
    }
    uint32_t fileNameLen = (uint32_t) strlen(value);
    if(fileNameLen == 0)
    {
        fprintf(stderr, "Error: usage: %s <file>\n", key);
        exit(413);
    }

    bool isJsonConfig = fileNameLen > strlen(".jsonc") && strcasecmp(value + fileNameLen - strlen(".jsonc"), ".jsonc") == 0;

    //Try to load as an absolute path

    if(isJsonConfig ? parseJsoncFile(value) : parseConfigFile(data, value))
        return;

    FF_STRBUF_AUTO_DESTROY absolutePath = ffStrbufCreateA(128);
    if (ffPathExpandEnv(value, &absolutePath))
    {
        bool success = isJsonConfig ? parseJsoncFile(absolutePath.chars) : parseConfigFile(data, absolutePath.chars);

        if(success)
            return;
    }

    //Try to load as a relative path

    FF_LIST_FOR_EACH(FFstrbuf, path, instance.state.platform.dataDirs)
    {
        //We need to copy it, because if a config file loads a config file, the value of path must be unchanged
        ffStrbufSet(&absolutePath, path);
        ffStrbufAppendS(&absolutePath, "fastfetch/presets/");
        ffStrbufAppendS(&absolutePath, value);

        bool success = isJsonConfig ? parseJsoncFile(absolutePath.chars) : parseConfigFile(data, absolutePath.chars);

        if(success)
            return;
    }

    {
        //Try exe path
        ffStrbufSet(&absolutePath, &instance.state.platform.exePath);
        ffStrbufSubstrBeforeLastC(&absolutePath, '/');
        ffStrbufAppendS(&absolutePath, "/presets/");
        ffStrbufAppendS(&absolutePath, value);

        bool success = isJsonConfig ? parseJsoncFile(absolutePath.chars) : parseConfigFile(data, absolutePath.chars);

        if(success)
            return;
    }

    //File not found

    fprintf(stderr, "Error: couldn't find config: %s\n", value);
    exit(414);
}

static inline void optionCheckString(const char* key, const char* value, FFstrbuf* buffer)
{
    if(value == NULL)
    {
        fprintf(stderr, "Error: usage: %s <str>\n", key);
        exit(477);
    }
    ffStrbufEnsureFree(buffer, 63); //This is not needed, as ffStrbufSetS will resize capacity if needed, but giving a higher start should improve performance
}

static void printVersion()
{
    FFVersionResult result = {};
    ffDetectVersion(&result);
    printf("%s %s%s%s (%s)\n", result.projectName, result.version, result.versionTweak, result.debugMode ? "-debug" : "", result.architecture);
}

static void parseOption(FFdata* data, const char* key, const char* value)
{
    ///////////////////////
    //Informative options//
    ///////////////////////

    if(ffStrEqualsIgnCase(key, "-h") || ffStrEqualsIgnCase(key, "--help"))
    {
        printCommandHelp(value);
        exit(0);
    }
    else if(ffStrEqualsIgnCase(key, "-v") || ffStrEqualsIgnCase(key, "--version"))
    {
        printVersion();
        exit(0);
    }
    else if(ffStrEqualsIgnCase(key, "--version-raw"))
    {
        puts(FASTFETCH_PROJECT_VERSION);
        exit(0);
    }
    else if(ffStrStartsWithIgnCase(key, "--print-"))
    {
        const char* subkey = key + strlen("--print-");
        if(ffStrEqualsIgnCase(subkey, "config-system"))
        {
            puts(FASTFETCH_DATATEXT_CONFIG_SYSTEM);
            exit(0);
        }
        else if(ffStrEqualsIgnCase(subkey, "config-user"))
        {
            puts(FASTFETCH_DATATEXT_CONFIG_USER);
            exit(0);
        }
        else if(ffStrEqualsIgnCase(subkey, "structure"))
        {
            puts(FASTFETCH_DATATEXT_STRUCTURE);
            exit(0);
        }
        else if(ffStrEqualsIgnCase(subkey, "logos"))
        {
            ffLogoBuiltinPrint();
            exit(0);
        }
        else
            goto error;
    }
    else if(ffStrStartsWithIgnCase(key, "--list-"))
    {
        const char* subkey = key + strlen("--list-");
        if(ffStrEqualsIgnCase(subkey, "modules"))
        {
            listModules();
            exit(0);
        }
        else if(ffStrEqualsIgnCase(subkey, "presets"))
        {
            listAvailablePresets();
            exit(0);
        }
        else if(ffStrEqualsIgnCase(subkey, "config-paths"))
        {
            listConfigPaths();
            exit(0);
        }
        else if(ffStrEqualsIgnCase(subkey, "data-paths"))
        {
            listDataPaths();
            exit(0);
        }
        else if(ffStrEqualsIgnCase(subkey, "features"))
        {
            ffListFeatures();
            exit(0);
        }
        else if(ffStrEqualsIgnCase(subkey, "logos"))
        {
            puts("Builtin logos:");
            ffLogoBuiltinList();
            puts("\nCustom logos:");
            listAvailableLogos();
            exit(0);
        }
        else if(ffStrEqualsIgnCase(subkey, "logos-autocompletion"))
        {
            ffLogoBuiltinListAutocompletion();
            exit(0);
        }
        else
            goto error;
    }
    else if(ffStrEqualsIgnCase(key, "--set") || ffStrEqualsIgnCase(key, "--set-keyless"))
    {
        FF_STRBUF_AUTO_DESTROY customValueStr = ffStrbufCreate();
        ffOptionParseString(key, value, &customValueStr);
        uint32_t index = ffStrbufFirstIndexC(&customValueStr, '=');
        if(index == 0 || index == customValueStr.length)
        {
            fprintf(stderr, "Error: usage: %s <key>=<str>\n", key);
            exit(477);
        }

        FF_STRBUF_AUTO_DESTROY customKey = ffStrbufCreateNS(index, customValueStr.chars);

        FFCustomValue* customValue = NULL;
        FF_LIST_FOR_EACH(FFCustomValue, x, data->customValues)
        {
            if(ffStrbufEqual(&x->key, &customKey))
            {
                ffStrbufDestroy(&x->key);
                ffStrbufDestroy(&x->value);
                customValue = x;
                break;
            }
        }
        if(!customValue) customValue = (FFCustomValue*) ffListAdd(&data->customValues);
        ffStrbufInitMove(&customValue->key, &customKey);
        ffStrbufSubstrAfter(&customValueStr, index);
        ffStrbufInitMove(&customValue->value, &customValueStr);
        customValue->printKey = key[5] == '\0';
    }

    ///////////////////
    //General options//
    ///////////////////

    else if(ffStrEqualsIgnCase(key, "-c") || ffStrEqualsIgnCase(key, "--load-config") || ffStrEqualsIgnCase(key, "--config"))
        optionParseConfigFile(data, key, value);
    else if(ffStrEqualsIgnCase(key, "--gen-config"))
        generateConfigFile(false, value);
    else if(ffStrEqualsIgnCase(key, "--gen-config-force"))
        generateConfigFile(true, value);
    else if(ffStrEqualsIgnCase(key, "--thread") || ffStrEqualsIgnCase(key, "--multithreading"))
        instance.config.multithreading = ffOptionParseBoolean(value);
    else if(ffStrEqualsIgnCase(key, "--stat"))
    {
        if((instance.config.stat = ffOptionParseBoolean(value)))
            instance.config.showErrors = true;
    }
    else if(ffStrEqualsIgnCase(key, "--allow-slow-operations"))
        instance.config.allowSlowOperations = ffOptionParseBoolean(value);
    else if(ffStrEqualsIgnCase(key, "--escape-bedrock"))
        instance.config.escapeBedrock = ffOptionParseBoolean(value);
    else if(ffStrEqualsIgnCase(key, "--pipe"))
        instance.config.pipe = ffOptionParseBoolean(value);
    else if(ffStrEqualsIgnCase(key, "--load-user-config"))
        data->loadUserConfig = ffOptionParseBoolean(value);
    else if(ffStrEqualsIgnCase(key, "--processing-timeout"))
        instance.config.processingTimeout = ffOptionParseInt32(key, value);
    else if(ffStrEqualsIgnCase(key, "--format"))
    {
        switch (ffOptionParseEnum(key, value, (FFKeyValuePair[]) {
            { "default", 0},
            { "json", 1 },
            {},
        }))
        {
            case 0:
                if (instance.state.resultDoc)
                {
                    yyjson_mut_doc_free(instance.state.resultDoc);
                    instance.state.resultDoc = NULL;
                }
                break;
            case 1:
                if (!instance.state.resultDoc)
                {
                    instance.state.resultDoc = yyjson_mut_doc_new(NULL);
                    yyjson_mut_doc_set_root(instance.state.resultDoc, yyjson_mut_arr(instance.state.resultDoc));
                }
                break;
        }
    }

    #if defined(__linux__) || defined(__FreeBSD__)
    else if(ffStrEqualsIgnCase(key, "--player-name"))
        ffOptionParseString(key, value, &instance.config.playerName);
    else if (ffStrEqualsIgnCase(key, "--os-file"))
        ffOptionParseString(key, value, &instance.config.osFile);
    else if(ffStrEqualsIgnCase(key, "--ds-force-drm"))
        instance.config.dsForceDrm = ffOptionParseBoolean(value);
    #elif defined(_WIN32)
    else if (ffStrEqualsIgnCase(key, "--wmi-timeout"))
        instance.config.wmiTimeout = ffOptionParseInt32(key, value);
    #endif

    ////////////////
    //Logo options//
    ////////////////

    else if(ffParseLogoCommandOptions(&instance.config.logo, key, value)) {}

    ///////////////////
    //Display options//
    ///////////////////

    else if(ffStrEqualsIgnCase(key, "--show-errors"))
        instance.config.showErrors = ffOptionParseBoolean(value);
    else if(ffStrEqualsIgnCase(key, "--disable-linewrap"))
        instance.config.disableLinewrap = ffOptionParseBoolean(value);
    else if(ffStrEqualsIgnCase(key, "--hide-cursor"))
        instance.config.hideCursor = ffOptionParseBoolean(value);
    else if(ffStrEqualsIgnCase(key, "-s") || ffStrEqualsIgnCase(key, "--structure"))
        ffOptionParseString(key, value, &data->structure);
    else if(ffStrEqualsIgnCase(key, "--separator"))
        ffOptionParseString(key, value, &instance.config.keyValueSeparator);
    else if(ffStrEqualsIgnCase(key, "--color"))
    {
        optionCheckString(key, value, &instance.config.colorKeys);
        ffOptionParseColor(value, &instance.config.colorKeys);
        ffStrbufSet(&instance.config.colorTitle, &instance.config.colorKeys);
    }
    else if(ffStrStartsWithIgnCase(key, "--color-"))
    {
        const char* subkey = key + strlen("--color-");
        if(ffStrEqualsIgnCase(subkey, "keys"))
        {
            optionCheckString(key, value, &instance.config.colorKeys);
            ffOptionParseColor(value, &instance.config.colorKeys);
        }
        else if(ffStrEqualsIgnCase(subkey, "title"))
        {
            optionCheckString(key, value, &instance.config.colorTitle);
            ffOptionParseColor(value, &instance.config.colorTitle);
        }
        else
            goto error;
    }
    else if(ffStrEqualsIgnCase(key, "--key-width"))
        instance.config.keyWidth = ffOptionParseUInt32(key, value);
    else if(ffStrEqualsIgnCase(key, "--bright-color"))
        instance.config.brightColor = ffOptionParseBoolean(value);
    else if(ffStrEqualsIgnCase(key, "--binary-prefix"))
    {
        instance.config.binaryPrefixType = (FFBinaryPrefixType) ffOptionParseEnum(key, value, (FFKeyValuePair[]) {
            { "iec", FF_BINARY_PREFIX_TYPE_IEC },
            { "si", FF_BINARY_PREFIX_TYPE_SI },
            { "jedec", FF_BINARY_PREFIX_TYPE_JEDEC },
            {}
        });
    }
    else if(ffStrEqualsIgnCase(key, "--size-ndigits"))
        instance.config.sizeNdigits = (uint8_t) ffOptionParseUInt32(key, value);
    else if(ffStrEqualsIgnCase(key, "--size-max-prefix"))
    {
        instance.config.sizeMaxPrefix = (uint8_t) ffOptionParseEnum(key, value, (FFKeyValuePair[]) {
            { "B", 0 },
            { "kB", 1 },
            { "MB", 2 },
            { "GB", 3 },
            { "TB", 4 },
            { "PB", 5 },
            { "EB", 6 },
            { "ZB", 7 },
            { "YB", 8 },
            {}
        });
    }
    else if(ffStrEqualsIgnCase(key, "--temperature-unit"))
    {
        instance.config.temperatureUnit = (FFTemperatureUnit) ffOptionParseEnum(key, value, (FFKeyValuePair[]) {
            { "CELSIUS", FF_TEMPERATURE_UNIT_CELSIUS },
            { "C", FF_TEMPERATURE_UNIT_CELSIUS },
            { "FAHRENHEIT", FF_TEMPERATURE_UNIT_FAHRENHEIT },
            { "F", FF_TEMPERATURE_UNIT_FAHRENHEIT },
            { "KELVIN", FF_TEMPERATURE_UNIT_KELVIN },
            { "K", FF_TEMPERATURE_UNIT_KELVIN },
            {},
        });
    }
    else if(ffStrEqualsIgnCase(key, "--percent-type"))
        instance.config.percentType = (uint8_t) ffOptionParseUInt32(key, value);
    else if(ffStrEqualsIgnCase(key, "--percent-ndigits"))
        instance.config.percentNdigits = (uint8_t) ffOptionParseUInt32(key, value);
    else if(ffStrEqualsIgnCase(key, "--no-buffer"))
        instance.config.noBuffer = ffOptionParseBoolean(value);
    else if(ffStrStartsWithIgnCase(key, "--bar-"))
    {
        const char* subkey = key + strlen("--bar-");
        if(ffStrEqualsIgnCase(subkey, "char-elapsed"))
            ffOptionParseString(key, value, &instance.config.barCharElapsed);
        else if(ffStrEqualsIgnCase(subkey, "char-total"))
            ffOptionParseString(key, value, &instance.config.barCharTotal);
        else if(ffStrEqualsIgnCase(subkey, "width"))
            instance.config.barWidth = (uint8_t) ffOptionParseUInt32(key, value);
        else if(ffStrEqualsIgnCase(subkey, "border"))
            instance.config.barBorder = ffOptionParseBoolean(value);
        else
            goto error;
    }

    ///////////////////
    //Library options//
    ///////////////////

    else if(ffStrStartsWithIgnCase(key, "--lib-"))
    {
        const char* subkey = key + strlen("--lib-");
        if(ffStrEqualsIgnCase(subkey, "PCI"))
            ffOptionParseString(key, value, &instance.config.libPCI);
        else if(ffStrEqualsIgnCase(subkey, "vulkan"))
            ffOptionParseString(key, value, &instance.config.libVulkan);
        else if(ffStrEqualsIgnCase(subkey, "freetype"))
            ffOptionParseString(key, value, &instance.config.libfreetype);
        else if(ffStrEqualsIgnCase(subkey, "wayland"))
            ffOptionParseString(key, value, &instance.config.libWayland);
        else if(ffStrEqualsIgnCase(subkey, "xcb-randr"))
            ffOptionParseString(key, value, &instance.config.libXcbRandr);
        else if(ffStrEqualsIgnCase(subkey, "xcb"))
            ffOptionParseString(key, value, &instance.config.libXcb);
        else if(ffStrEqualsIgnCase(subkey, "Xrandr"))
            ffOptionParseString(key, value, &instance.config.libXrandr);
        else if(ffStrEqualsIgnCase(subkey, "X11"))
            ffOptionParseString(key, value, &instance.config.libX11);
        else if(ffStrEqualsIgnCase(subkey, "gio"))
            ffOptionParseString(key, value, &instance.config.libGIO);
        else if(ffStrEqualsIgnCase(subkey, "DConf"))
            ffOptionParseString(key, value, &instance.config.libDConf);
        else if(ffStrEqualsIgnCase(subkey, "dbus"))
            ffOptionParseString(key, value, &instance.config.libDBus);
        else if(ffStrEqualsIgnCase(subkey, "XFConf"))
            ffOptionParseString(key, value, &instance.config.libXFConf);
        else if(ffStrEqualsIgnCase(subkey, "sqlite") || ffStrEqualsIgnCase(subkey, "sqlite3"))
            ffOptionParseString(key, value, &instance.config.libSQLite3);
        else if(ffStrEqualsIgnCase(subkey, "rpm"))
            ffOptionParseString(key, value, &instance.config.librpm);
        else if(ffStrEqualsIgnCase(subkey, "imagemagick"))
            ffOptionParseString(key, value, &instance.config.libImageMagick);
        else if(ffStrEqualsIgnCase(subkey, "z"))
            ffOptionParseString(key, value, &instance.config.libZ);
        else if(ffStrEqualsIgnCase(subkey, "chafa"))
            ffOptionParseString(key, value, &instance.config.libChafa);
        else if(ffStrEqualsIgnCase(subkey, "egl"))
            ffOptionParseString(key, value, &instance.config.libEGL);
        else if(ffStrEqualsIgnCase(subkey, "glx"))
            ffOptionParseString(key, value, &instance.config.libGLX);
        else if(ffStrEqualsIgnCase(subkey, "osmesa"))
            ffOptionParseString(key, value, &instance.config.libOSMesa);
        else if(ffStrEqualsIgnCase(subkey, "opencl"))
            ffOptionParseString(key, value, &instance.config.libOpenCL);
        else if(ffStrEqualsIgnCase(subkey, "pulse"))
            ffOptionParseString(key, value, &instance.config.libPulse);
        else if(ffStrEqualsIgnCase(subkey, "nm"))
            ffOptionParseString(key, value, &instance.config.libnm);
        else if(ffStrEqualsIgnCase(subkey, "ddcutil"))
            ffOptionParseString(key, value, &instance.config.libDdcutil);
        else
            goto error;
    }

    ///////////////////////
    //Module args options//
    ///////////////////////

    else if(ffParseModuleOptions(key, value)) {}

    //////////////////
    //Unknown option//
    //////////////////

    else
    {
error:
        fprintf(stderr, "Error: unknown option: %s\n", key);
        exit(400);
    }
}

static void parseConfigFiles(FFdata* data)
{
    for(uint32_t i = instance.state.platform.configDirs.length; i > 0; --i)
    {
        FFstrbuf* dir = ffListGet(&instance.state.platform.configDirs, i - 1);
        uint32_t dirLength = dir->length;

        ffStrbufAppendS(dir, "fastfetch/config.jsonc");
        bool success = parseJsoncFile(dir->chars);
        ffStrbufSubstrBefore(dir, dirLength);
        if (success) return;
    }

    for(uint32_t i = instance.state.platform.configDirs.length; i > 0; --i)
    {
        if(!data->loadUserConfig)
            return;

        FFstrbuf* dir = ffListGet(&instance.state.platform.configDirs, i - 1);
        uint32_t dirLength = dir->length;

        ffStrbufAppendS(dir, "fastfetch/config.conf");
        parseConfigFile(data, dir->chars);
        ffStrbufSubstrBefore(dir, dirLength);
    }
}

static void parseArguments(FFdata* data, int argc, const char** argv)
{
    if(!data->loadUserConfig)
        return;

    for(int i = 1; i < argc; i++)
    {
        const char* key = argv[i];
        if(*key != '-')
        {
            fprintf(stderr, "Error: invalid option: %s. An option must start with `-`\n", key);
            exit(400);
        }

        if(i == argc - 1 || (
            argv[i + 1][0] == '-' &&
            argv[i + 1][1] != '\0' && // `-` is used as an alias for `/dev/stdin`
            strcasecmp(argv[i], "--separator-string") != 0 // Separator string can start with a -
        )) {
            parseOption(data, argv[i], NULL);
        }
        else
        {
            parseOption(data, argv[i], argv[i + 1]);
            ++i;
        }
    }
}

int main(int argc, const char** argv)
{
    ffInitInstance();

    //Data stores things only needed for the configuration of fastfetch
    FFdata data;
    ffStrbufInit(&data.structure);
    ffListInit(&data.customValues, sizeof(FFCustomValue));
    data.loadUserConfig = true;

    if(!getenv("NO_CONFIG"))
        parseConfigFiles(&data);
    parseArguments(&data, argc, argv);

    if (instance.state.configDoc)
    {
        const char* error = NULL;

        if (
            (error = ffParseLogoJsonConfig()) ||
            (error = ffParseGeneralJsonConfig()) ||
            (error = ffParseDisplayJsonConfig()) ||
            (error = ffParseLibraryJsonConfig()) ||
            false
        ) {
            fputs(error, stderr);
            exit(477);
        }
    }

    const bool useJsonConfig = data.structure.length == 0 && instance.state.configDoc;

    if(useJsonConfig)
        ffPrintJsonConfig(true /* prepare */);
    else
        ffPrepareCommandOption(&data);

    ffStart();

    #if defined(_WIN32)
        if (!instance.config.noBuffer) fflush(stdout);
    #endif

    if (useJsonConfig)
        ffPrintJsonConfig(false);
    else
        ffPrintCommandOption(&data);

    if (instance.state.resultDoc)
    {
        yyjson_mut_write_fp(stdout, instance.state.resultDoc, YYJSON_WRITE_INF_AND_NAN_AS_NULL | YYJSON_WRITE_PRETTY_TWO_SPACES, NULL, NULL);
    }

    ffFinish();

    ffStrbufDestroy(&data.structure);
    FF_LIST_FOR_EACH(FFCustomValue, customValue, data.customValues)
    {
        ffStrbufDestroy(&customValue->key);
        ffStrbufDestroy(&customValue->value);
    }
    ffListDestroy(&data.customValues);

    ffDestroyInstance();
}
