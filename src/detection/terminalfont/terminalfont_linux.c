#include "terminalfont.h"
#include "common/settings.h"
#include "common/properties.h"
#include "common/parsing.h"
#include "detection/terminalshell/terminalshell.h"
#include "detection/displayserver/displayserver.h"
#include "util/mallocHelper.h"
#include "util/stringUtils.h"

static const char* getSystemMonospaceFont(void)
{
    const FFDisplayServerResult* wmde = ffConnectDisplayServer();

    if(ffStrbufIgnCaseEqualS(&wmde->dePrettyName, "Cinnamon"))
    {
        const char* systemMonospaceFont = ffSettingsGet("/org/cinnamon/desktop/interface/monospace-font-name", "org.cinnamon.desktop.interface", NULL, "monospace-font-name", FF_VARIANT_TYPE_STRING).strValue;
        if(ffStrSet(systemMonospaceFont))
            return systemMonospaceFont;
    }
    else if(ffStrbufIgnCaseEqualS(&wmde->dePrettyName, "Mate"))
    {
        const char* systemMonospaceFont = ffSettingsGet("/org/mate/interface/monospace-font-name", "org.mate.interface", NULL, "monospace-font-name", FF_VARIANT_TYPE_STRING).strValue;
        if(ffStrSet(systemMonospaceFont))
            return systemMonospaceFont;
    }

    return ffSettingsGet("/org/gnome/desktop/interface/monospace-font-name", "org.gnome.desktop.interface", NULL, "monospace-font-name", FF_VARIANT_TYPE_STRING).strValue;
}

static void detectKgx(FFTerminalFontResult* terminalFont)
{
    // kgx (gnome console) doesn't support profiles
    if(!ffSettingsGet("/org/gnome/Console/use-system-font", "org.gnome.Console", NULL, "use-system-font", FF_VARIANT_TYPE_BOOL).boolValue)
    {
        FF_AUTO_FREE const char* fontName = ffSettingsGet("/org/gnome/Console/custom-font", "org.gnome.Console", NULL, "custom-font", FF_VARIANT_TYPE_STRING).strValue;
        if(ffStrSet(fontName))
            ffFontInitPango(&terminalFont->font, fontName);
        else
            ffStrbufAppendF(&terminalFont->error, "Couldn't get terminal font from GSettings (org.gnome.Console::custom-font)");
    }
    else
    {
        FF_AUTO_FREE const char* fontName = getSystemMonospaceFont();
        if(ffStrSet(fontName))
            ffFontInitPango(&terminalFont->font, fontName);
        else
            ffStrbufAppendS(&terminalFont->error, "Could't get system monospace font name from GSettings / DConf");
    }
}

static void detectFromGSettings(const char* profilePath, const char* profileList, const char* profile, const char* defaultProfileKey, FFTerminalFontResult* terminalFont)
{
    FF_AUTO_FREE const char* defaultProfile = ffSettingsGetGSettings(profileList, NULL, defaultProfileKey, FF_VARIANT_TYPE_STRING).strValue;
    if(!ffStrSet(defaultProfile))
    {
        ffStrbufAppendF(&terminalFont->error, "Could not get default profile from gsettings: %s", profileList);
        return;
    }

    FF_STRBUF_AUTO_DESTROY path = ffStrbufCreateA(128);
    ffStrbufAppendS(&path, profilePath);
    ffStrbufAppendS(&path, defaultProfile);
    ffStrbufAppendC(&path, '/');

    if(!ffSettingsGetGSettings(profile, path.chars, "use-system-font", FF_VARIANT_TYPE_BOOL).boolValue)
    {
        FF_AUTO_FREE const char* fontName = ffSettingsGetGSettings(profile, path.chars, "font", FF_VARIANT_TYPE_STRING).strValue;
        if(ffStrSet(fontName))
            ffFontInitPango(&terminalFont->font, fontName);
        else
            ffStrbufAppendF(&terminalFont->error, "Couldn't get terminal font from GSettings (%s::%s::font)", profile, path.chars);
    }
    else
    {
        FF_AUTO_FREE const char* fontName = getSystemMonospaceFont();
        if(ffStrSet(fontName))
            ffFontInitPango(&terminalFont->font, fontName);
        else
            ffStrbufAppendS(&terminalFont->error, "Could't get system monospace font name from GSettings / DConf");
    }
}

static void detectFromConfigFile(const char* configFile, const char* start, FFTerminalFontResult* terminalFont)
{
    FF_STRBUF_AUTO_DESTROY fontName = ffStrbufCreate();
    ffParsePropFileConfig(configFile, start, &fontName);

    if(fontName.length == 0)
        ffStrbufAppendF(&terminalFont->error, "Couldn't find %s in .config/%s", start, configFile);
    else
        ffFontInitPango(&terminalFont->font, fontName.chars);
}

static void detectKonsole(FFTerminalFontResult* terminalFont, const char* rcFile)
{
    FF_STRBUF_AUTO_DESTROY profile = ffStrbufCreate();
    if(!ffParsePropFileConfig(rcFile, "DefaultProfile =", &profile))
    {
        ffStrbufAppendF(&terminalFont->error, "Configuration \".config/%s\" doesn't exist", rcFile);
        return;
    }

    if(profile.length == 0)
    {
        ffStrbufAppendS(&terminalFont->error, "Built-in profile is used");
        return;
    }

    FF_STRBUF_AUTO_DESTROY profilePath = ffStrbufCreateA(32);
    ffStrbufAppendS(&profilePath, "konsole/");
    ffStrbufAppend(&profilePath, &profile);

    FF_STRBUF_AUTO_DESTROY fontName = ffStrbufCreate();
    ffParsePropFileData(profilePath.chars, "Font =", &fontName);

    if(fontName.length == 0)
        ffStrbufAppendF(&terminalFont->error, "Couldn't find \"Font=%%[^\\n]\" in \"%s\"", profilePath.chars);
    else
        ffFontInitQt(&terminalFont->font, fontName.chars);
}

static void detectXFCETerminal(FFTerminalFontResult* terminalFont)
{
    FF_STRBUF_AUTO_DESTROY useSysFont = ffStrbufCreate();
    FF_STRBUF_AUTO_DESTROY fontName = ffStrbufCreate();

    ffParsePropFileConfigValues("xfce4/terminal/terminalrc", 2, (FFpropquery[]) {
        {"FontUseSystem = ", &useSysFont},
        {"FontName = ", &fontName}
    });

    if(useSysFont.length == 0 || ffStrbufIgnCaseCompS(&useSysFont, "false") == 0)
    {
        if(fontName.length == 0)
            ffStrbufAppendS(&terminalFont->error, "Couldn't find FontName in .config/xfce4/terminal/terminalrc");
        else
            ffFontInitPango(&terminalFont->font, fontName.chars);
    }
    else
    {
        const char* systemFontName = ffSettingsGetXFConf("xsettings", "/Gtk/MonospaceFontName", FF_VARIANT_TYPE_STRING).strValue;
        if(ffStrSet(systemFontName))
            ffFontInitPango(&terminalFont->font, systemFontName);
        else
            ffStrbufAppendS(&terminalFont->error, "Couldn't find xsettings::/Gtk/MonospaceFontName in XFConf");
    }
}

static void detectDeepinTerminal(FFTerminalFontResult* terminalFont)
{
    FF_STRBUF_AUTO_DESTROY fontName = ffStrbufCreate();
    FF_STRBUF_AUTO_DESTROY fontSize = ffStrbufCreate();
    FF_STRBUF_AUTO_DESTROY profile = ffStrbufCreate();

    ffStrbufAppend(&profile, &instance.state.platform.homeDir);
    ffStrbufAppendS(&profile, ".config/deepin/deepin-terminal/config.conf"); //TODO: Use config dirs
    FILE* file = fopen(profile.chars, "r");

    if(file)
    {
        char* line = NULL;
        size_t len = 0;

        for(int count = 0; getline(&line, &len, file) != -1 && count < 2;)
        {
            if(ffStrEquals(line, "[basic.interface.font]\n"))
            {
                if(getline(&line, &len, file) != -1)
                    ffParsePropLine(line, "value=", &fontName);
                ++count;
            }
            else if(ffStrEquals(line, "[basic.interface.font_size]\n"))
            {
                if(getline(&line, &len, file) != -1)
                    ffParsePropLine(line, "value=", &fontSize);
                ++count;
            }
        }

        free(line);
        fclose(file);
    }

    if(fontName.length == 0)
        ffStrbufAppendS(&fontName, "Noto Sans Mono");
    if(fontSize.length == 0)
        ffStrbufAppendS(&fontSize, "11");

    ffFontInitValues(&terminalFont->font, fontName.chars, fontSize.chars);
}

static void detectFootTerminal(FFTerminalFontResult* terminalFont)
{
    FF_STRBUF_AUTO_DESTROY font = ffStrbufCreate();

    if (!ffParsePropFileConfig("foot/foot.ini", "font=", &font) || !ffStrSet(font.chars))
    {
        ffFontInitValues(&terminalFont->font, "monospace", "8");
        return;
    }

    //Sarasa Term SC Nerd:size=8
    uint32_t colon = ffStrbufFirstIndexC(&font, ':');
    if(colon == font.length)
    {
        ffFontInitValues(&terminalFont->font, font.chars, "8");
        return;
    }
    uint32_t equal = ffStrbufNextIndexS(&font, colon, "size=");
    font.chars[colon] = 0;
    if (equal == font.length)
    {
        ffFontInitValues(&terminalFont->font, font.chars, "8");
        return;
    }
    ffFontInitValues(&terminalFont->font, font.chars, &font.chars[equal + strlen("size=")]);
}

static void detectQTerminal(FFTerminalFontResult* terminalFont)
{
    FF_STRBUF_AUTO_DESTROY fontName = ffStrbufCreate();
    FF_STRBUF_AUTO_DESTROY fontSize = ffStrbufCreate();

    ffParsePropFileConfigValues("qterminal.org/qterminal.ini", 2, (FFpropquery[]) {
        {"fontFamily=", &fontName},
        {"fontSize=", &fontSize},
    });

    if (fontName.length == 0)
        ffStrbufAppendS(&fontName, "monospace");
    if (fontSize.length == 0)
        ffStrbufAppendS(&fontSize, "12");
    ffFontInitValues(&terminalFont->font, fontName.chars, fontSize.chars);
}

static void detectXterm(FFTerminalFontResult* terminalFont)
{
    FF_STRBUF_AUTO_DESTROY fontName = ffStrbufCreate();
    FF_STRBUF_AUTO_DESTROY fontSize = ffStrbufCreate();

    ffParsePropFileHomeValues(".Xresources", 2, (FFpropquery[]) {
        {"xterm*faceName:", &fontName},
        {"xterm*faceSize:", &fontSize},
    });

    if (fontName.length == 0)
        ffStrbufAppendS(&fontName, "fixed");
    if (fontSize.length == 0)
        ffStrbufAppendS(&fontSize, "8.0");
    ffFontInitValues(&terminalFont->font, fontName.chars, fontSize.chars);
}

void ffDetectTerminalFontPlatform(const FFTerminalShellResult* terminalShell, FFTerminalFontResult* terminalFont)
{
    if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "konsole"))
        detectKonsole(terminalFont, "konsolerc");
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "yakuake"))
        detectKonsole(terminalFont, "yakuakerc");
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "xfce4-terminal"))
        detectXFCETerminal(terminalFont);
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "lxterminal"))
        detectFromConfigFile("lxterminal/lxterminal.conf", "fontname =", terminalFont);
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "tilix"))
        detectFromGSettings("/com/gexperts/Tilix/profiles/", "com.gexperts.Tilix.ProfilesList", "com.gexperts.Tilix.Profile", "default", terminalFont);
    else if(ffStrbufStartsWithIgnCaseS(&terminalShell->terminalProcessName, "gnome-terminal-"))
        detectFromGSettings("/org/gnome/terminal/legacy/profiles:/:", "org.gnome.Terminal.ProfilesList", "org.gnome.Terminal.Legacy.Profile", "default", terminalFont);
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "kgx"))
        detectKgx(terminalFont);
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "mate-terminal"))
        detectFromGSettings("/org/mate/terminal/profiles/", "org.mate.terminal.global", "org.mate.terminal.profile", "default-profile", terminalFont);
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "deepin-terminal"))
        detectDeepinTerminal(terminalFont);
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "foot"))
        detectFootTerminal(terminalFont);
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "qterminal"))
        detectQTerminal(terminalFont);
    else if(ffStrbufIgnCaseEqualS(&terminalShell->terminalProcessName, "xterm"))
        detectXterm(terminalFont);
}
