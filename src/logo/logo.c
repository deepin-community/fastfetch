#include "logo/logo.h"
#include "common/io/io.h"
#include "common/printing.h"
#include "detection/os/os.h"
#include "detection/terminalshell/terminalshell.h"
#include "util/textModifier.h"
#include "util/stringUtils.h"

#include <ctype.h>
#include <string.h>

typedef enum FFLogoSize
{
    FF_LOGO_SIZE_UNKNOWN,
    FF_LOGO_SIZE_NORMAL,
    FF_LOGO_SIZE_SMALL,
} FFLogoSize;

static void ffLogoPrintCharsRaw(const char* data, size_t length)
{
    FFLogoOptions* options = &instance.config.logo;

    if (instance.config.brightColor)
        fputs(FASTFETCH_TEXT_MODIFIER_BOLT, stdout);

    ffPrintCharTimes('\n', options->paddingTop);
    ffPrintCharTimes(' ', options->paddingLeft);
    fwrite(data, length, 1, stdout);
    instance.state.logoHeight = options->paddingTop + options->height;
    instance.state.logoWidth = options->paddingLeft + options->width + options->paddingRight;
    printf("\033[9999999D\n\033[%uA", instance.state.logoHeight);
}

void ffLogoPrintChars(const char* data, bool doColorReplacement)
{
    FFLogoOptions* options = &instance.config.logo;

    uint32_t currentlineLength = 0;

    if (instance.config.brightColor)
        fputs(FASTFETCH_TEXT_MODIFIER_BOLT, stdout);

    ffPrintCharTimes('\n', options->paddingTop);
    ffPrintCharTimes(' ', options->paddingLeft);

    instance.state.logoHeight = options->paddingTop;

    //Use logoColor[0] as the default color
    if(doColorReplacement)
        ffPrintColor(&options->colors[0]);

    while(*data != '\0')
    {
        //We are at the end of a line. Print paddings and update max line length
        if(*data == '\n' || (*data == '\r' && *(data + 1) == '\n'))
        {
            ffPrintCharTimes(' ', options->paddingRight);

            //We have \r\n, skip the \r
            if(*data == '\r')
                ++data;

            putchar('\n');
            ++data;

            ffPrintCharTimes(' ', options->paddingLeft);

            if(currentlineLength > instance.state.logoWidth)
                instance.state.logoWidth = currentlineLength;

            currentlineLength = 0;
            ++instance.state.logoHeight;
            continue;
        }

        //Always print tabs as 4 spaces, to have consistent spacing
        if(*data == '\t')
        {
            ffPrintCharTimes(' ', 4);
            ++data;
            continue;
        }

        //We have an escape sequence direclty as bytes. We print it, but don't increase the line length
        if(*data == '\033' && *(data + 1) == '[')
        {
            const char* start = data;

            fputs("\033[", stdout);
            data += 2;

            while(isdigit(*data) || *data == ';')
                putchar(*data++); // number

            //We have a valid control sequence, print it and continue with next char
            if(isascii(*data))
            {
                putchar(*data++); // single letter, end of control sequence
                continue;
            }

            //Invalid control sequence, try to get most accurate length
            currentlineLength += (uint32_t) (data - start - 1); //-1 for \033 which for sure doesn't take any space

            //Don't continue here, print the char after the letters with the unicode printing
        }

        //We have a fastfetch color placeholder. Replace it with the esacape sequence, don't increase the line length
        if(doColorReplacement && *data == '$')
        {
            ++data;

            //If we have $$, or $\0, print it as single $
            if(*data == '$' || *data == '\0')
            {
                putchar('$');
                ++currentlineLength;
                ++data;
                continue;
            }

            //Map the number to an array index, so that '1' -> 0, '2' -> 1, etc.
            int index = ((int) *data) - 49;

            //If the index is valid, print the color. Otherwise continue as normal
            if(index < 0 || index >= FASTFETCH_LOGO_MAX_COLORS)
            {
                putchar('$');
                ++currentlineLength;
                //Don't continue here, we want to print the current char as unicode
            }
            else
            {
                ffPrintColor(&options->colors[index]);
                ++data;
                continue;
            }
        }

        //Do the printing, respecting unicode

        ++currentlineLength;

        int codepoint = (unsigned char) *data;
        uint8_t bytes;

        if(codepoint <= 127)
            bytes = 1;
        else if((codepoint & 0xE0) == 0xC0)
            bytes = 2;
        else if((codepoint & 0xF0) == 0xE0)
            bytes = 3;
        else if((codepoint & 0xF8) == 0xF0)
            bytes = 4;
        else
            bytes = 1; //Invalid utf8, print it as is, byte by byte

        for(uint8_t i = 0; i < bytes; ++i)
        {
            if(*data == '\0')
                break;

            putchar(*data++);
        }
    }

    ffPrintCharTimes(' ', options->paddingRight);
    fputs(FASTFETCH_TEXT_MODIFIER_RESET, stdout);

    //Happens if the last line is the longest
    if(currentlineLength > instance.state.logoWidth)
        instance.state.logoWidth = currentlineLength;

    instance.state.logoWidth += options->paddingLeft + options->paddingRight;

    //Go to the leftmost position
    fputs("\033[9999999D", stdout);

    //If the logo height is > 1, go up the height
    if(instance.state.logoHeight > 0)
        printf("\033[%uA", instance.state.logoHeight);
}

static void logoApplyColors(const FFlogo* logo)
{
    if(instance.config.colorKeys.length == 0)
        ffStrbufAppendS(&instance.config.colorKeys, logo->colorKeys ? logo->colorKeys : logo->colors[0]);

    if(instance.config.colorTitle.length == 0)
        ffStrbufAppendS(&instance.config.colorTitle, logo->colorTitle ? logo->colorTitle : logo->colors[1]);
}

static bool logoHasName(const FFlogo* logo, const FFstrbuf* name, bool small)
{
    for(
        const char* const* logoName = logo->names;
        *logoName != NULL && logoName <= &logo->names[FASTFETCH_LOGO_MAX_NAMES];
        ++logoName
    ) {
        if(small)
        {
            uint32_t logoNameLength = (uint32_t) (strlen(*logoName) - strlen("_small"));
            if(name->length == logoNameLength && strncasecmp(*logoName, name->chars, logoNameLength) == 0) return true;
        }
        if(ffStrbufIgnCaseEqualS(name, *logoName))
            return true;
    }

    return false;
}

static const FFlogo* logoGetBuiltin(const FFstrbuf* name, FFLogoSize size)
{
    if (name->length == 0 || !isalpha(name->chars[0]))
        return NULL;

    for(const FFlogo* logo = ffLogoBuiltins[toupper(name->chars[0]) - 'A']; *logo->names; ++logo)
    {
        switch (size)
        {
            // Never use alternate logos
            case FF_LOGO_SIZE_NORMAL:
                if(logo->type != FF_LOGO_LINE_TYPE_NORMAL) continue;
                break;
            case FF_LOGO_SIZE_SMALL:
                if(logo->type != FF_LOGO_LINE_TYPE_SMALL_BIT) continue;
                break;
            default:
                break;
        }

        if(logoHasName(logo, name, size == FF_LOGO_SIZE_SMALL))
            return logo;
    }

    return NULL;
}

static const FFlogo* logoGetBuiltinDetected(FFLogoSize size)
{
    const FFOSResult* os = ffDetectOS();

    const FFlogo* logo = logoGetBuiltin(&os->id, size);
    if(logo != NULL)
        return logo;

    logo = logoGetBuiltin(&os->name, size);
    if(logo != NULL)
        return logo;

    logo = logoGetBuiltin(&os->prettyName, size);
    if(logo != NULL)
        return logo;

    logo = logoGetBuiltin(&os->idLike, size);
    if(logo != NULL)
        return logo;

    logo = logoGetBuiltin(&instance.state.platform.systemName, size);
    if(logo != NULL)
        return logo;

    return &ffLogoUnknown;
}

static inline void logoApplyColorsDetected(void)
{
    logoApplyColors(logoGetBuiltinDetected(FF_LOGO_SIZE_NORMAL));
}

static void logoPrintStruct(const FFlogo* logo)
{
    logoApplyColors(logo);

    FFLogoOptions* options = &instance.config.logo;

    const char* const* colors = logo->colors;
    for(int i = 0; *colors != NULL && i < FASTFETCH_LOGO_MAX_COLORS; i++, colors++)
    {
        if(options->colors[i].length == 0)
            ffStrbufAppendS(&options->colors[i], *colors);
    }

    ffLogoPrintChars(logo->lines, true);
}

static void logoPrintNone(void)
{
    logoApplyColorsDetected();
    instance.state.logoHeight = 0;
    instance.state.logoWidth = 0;
}

static bool logoPrintBuiltinIfExists(const FFstrbuf* name, FFLogoSize size)
{
    if(ffStrbufIgnCaseEqualS(name, "none"))
    {
        logoPrintNone();
        return true;
    }

    const FFlogo* logo = logoGetBuiltin(name, size);
    if(logo == NULL)
        return false;

    logoPrintStruct(logo);

    return true;
}

static inline void logoPrintDetected(FFLogoSize size)
{
    logoPrintStruct(logoGetBuiltinDetected(size));
}

static bool logoPrintData(bool doColorReplacement)
{
    FFLogoOptions* options = &instance.config.logo;
    if(options->source.length == 0)
        return false;

    ffLogoPrintChars(options->source.chars, doColorReplacement);
    logoApplyColorsDetected();
    return true;
}

static void updateLogoPath(void)
{
    FFLogoOptions* options = &instance.config.logo;

    if(ffPathExists(options->source.chars, FF_PATHTYPE_FILE))
        return;

    FF_STRBUF_AUTO_DESTROY fullPath = ffStrbufCreate();
    if (ffPathExpandEnv(options->source.chars, &fullPath) && ffPathExists(fullPath.chars, FF_PATHTYPE_FILE))
    {
        ffStrbufSet(&options->source, &fullPath);
        return;
    }

    FF_LIST_FOR_EACH(FFstrbuf, dataDir, instance.state.platform.dataDirs)
    {
        //We need to copy it, because multiple threads might be using dataDirs at the same time
        ffStrbufSet(&fullPath, dataDir);
        ffStrbufAppendS(&fullPath, "fastfetch/logos/");
        ffStrbufAppend(&fullPath, &options->source);

        if(ffPathExists(fullPath.chars, FF_PATHTYPE_FILE))
        {
            ffStrbufSet(&options->source, &fullPath);
            break;
        }
    }
}

static bool logoPrintFileIfExists(bool doColorReplacement, bool raw)
{
    FFLogoOptions* options = &instance.config.logo;

    FF_STRBUF_AUTO_DESTROY content = ffStrbufCreate();

    if(ffStrbufEqualS(&options->source, "-")
        ? !ffAppendFDBuffer(FFUnixFD2NativeFD(STDIN_FILENO), &content)
        : !ffAppendFileBuffer(options->source.chars, &content)
    )
    {
        fprintf(stderr, "Logo: Failed to load file content from logo source: %s \n", options->source.chars);
        return false;
    }

    logoApplyColorsDetected();
    if(raw)
        ffLogoPrintCharsRaw(content.chars, content.length);
    else
        ffLogoPrintChars(content.chars, doColorReplacement);

    return true;
}

static bool logoPrintImageIfExists(FFLogoType logo, bool printError)
{
    if(!ffLogoPrintImageIfExists(logo, printError))
        return false;

    logoApplyColorsDetected();
    return true;
}

static bool logoTryKnownType(void)
{
    FFLogoOptions* options = &instance.config.logo;

    if(options->type == FF_LOGO_TYPE_NONE)
    {
        logoApplyColorsDetected();
        return true;
    }

    if(options->type == FF_LOGO_TYPE_BUILTIN)
        return logoPrintBuiltinIfExists(&options->source, FF_LOGO_SIZE_NORMAL);

    if(options->type == FF_LOGO_TYPE_SMALL)
        return logoPrintBuiltinIfExists(&options->source, FF_LOGO_SIZE_SMALL);

    if(options->type == FF_LOGO_TYPE_DATA)
        return logoPrintData(true);

    if(options->type == FF_LOGO_TYPE_DATA_RAW)
        return logoPrintData(false);

    updateLogoPath(); //We sure have a file, resolve relative paths

    if(options->type == FF_LOGO_TYPE_FILE)
        return logoPrintFileIfExists(true, false);

    if(options->type == FF_LOGO_TYPE_FILE_RAW)
        return logoPrintFileIfExists(false, false);

    if(options->type == FF_LOGO_TYPE_IMAGE_RAW)
    {
        if(options->width == 0 || options->height == 0)
        {
            fputs("both `--logo-width` and `--logo-height` must be specified\n", stderr);
            return false;
        }

        return logoPrintFileIfExists(false, true);
    }

    return logoPrintImageIfExists(options->type, true);
}

static void logoPrintKnownType(void)
{
    if(!logoTryKnownType())
        logoPrintDetected(FF_LOGO_SIZE_UNKNOWN);
}

void ffLogoPrint(void)
{
    //In pipe mode, we don't have a logo or padding.
    //We also don't need to set main color, because it won't be printed anyway.
    //So we can return quickly here.
    if(instance.config.pipe)
    {
        instance.state.logoHeight = 0;
        instance.state.logoWidth = 0;
        return;
    }

    const FFLogoOptions* options = &instance.config.logo;

    if (options->type == FF_LOGO_TYPE_NONE)
    {
        logoPrintNone();
        return;
    }

    //If the source is not set, we can directly print the detected logo.
    if(options->source.length == 0)
    {
        logoPrintDetected(options->type == FF_LOGO_TYPE_SMALL ? FF_LOGO_SIZE_SMALL : FF_LOGO_SIZE_NORMAL);
        return;
    }

    //If the source and source type is set to something else than auto, always print with the set type.
    if(options->source.length > 0 && options->type != FF_LOGO_TYPE_AUTO)
    {
        logoPrintKnownType();
        return;
    }

    //If source matches the name of a builtin logo, print it and return.
    if(logoPrintBuiltinIfExists(&options->source, FF_LOGO_SIZE_UNKNOWN))
        return;

    //Make sure the logo path is set correctly.
    updateLogoPath();

    const FFTerminalShellResult* terminalShell = ffDetectTerminalShell();

    //Terminal emulators that support kitty graphics protocol.
    bool supportsKitty =
        ffStrbufIgnCaseCompS(&terminalShell->terminalProcessName, "kitty") == 0 ||
        ffStrbufIgnCaseCompS(&terminalShell->terminalProcessName, "konsole") == 0 ||
        ffStrbufIgnCaseCompS(&terminalShell->terminalProcessName, "wezterm") == 0 ||
        ffStrbufIgnCaseCompS(&terminalShell->terminalProcessName, "wayst") == 0;

    //Try to load the logo as an image. If it succeeds, print it and return.
    if(logoPrintImageIfExists(supportsKitty ? FF_LOGO_TYPE_IMAGE_KITTY : FF_LOGO_TYPE_IMAGE_CHAFA, false))
        return;

    //Try to load the logo as a file. If it succeeds, print it and return.
    if(logoPrintFileIfExists(true, false))
        return;

    logoPrintDetected(FF_LOGO_SIZE_UNKNOWN);
}

void ffLogoPrintLine(void)
{
    if(instance.state.logoWidth > 0)
        printf("\033[%uC", instance.state.logoWidth);

    ++instance.state.keysHeight;
}

void ffLogoPrintRemaining(void)
{
    if (instance.state.keysHeight <= instance.state.logoHeight)
        ffPrintCharTimes('\n', instance.state.logoHeight - instance.state.keysHeight + 1);
    instance.state.keysHeight = instance.state.logoHeight + 1;
}

void ffLogoBuiltinPrint(void)
{
    FFLogoOptions* options = &instance.config.logo;

    for(uint8_t ch = 0; ch < 26; ++ch)
    {
        for(const FFlogo* logo = ffLogoBuiltins[ch]; *logo->names; ++logo)
        {
            printf("\033[%sm%s:\033[0m\n", logo->colors[0], logo->names[0]);
            logoPrintStruct(logo);
            ffLogoPrintRemaining();

            //reset everything
            instance.state.logoHeight = 0;
            instance.state.keysHeight = 0;
            for(uint8_t i = 0; i < FASTFETCH_LOGO_MAX_COLORS; i++)
                ffStrbufClear(&options->colors[i]);

            putchar('\n');
        }
    }
}

void ffLogoBuiltinList(void)
{
    uint32_t counter = 0;
    for(uint8_t ch = 0; ch < 26; ++ch)
    {
        for(const FFlogo* logo = ffLogoBuiltins[ch]; *logo->names; ++logo)
        {
            ++counter;
            printf("%u)%s ", counter, counter < 10 ? " " : "");

            for(
                const char* const* names = logo->names;
                *names != NULL && names <= &logo->names[FASTFETCH_LOGO_MAX_NAMES];
                ++names
            )
                printf("\"%s\" ", *names);

            putchar('\n');
        }
    }
}

void ffLogoBuiltinListAutocompletion(void)
{
    for(uint8_t ch = 0; ch < 26; ++ch)
    {
        for(const FFlogo* logo = ffLogoBuiltins[ch]; *logo->names; ++logo)
            printf("%s\n", logo->names[0]);
    }
}
