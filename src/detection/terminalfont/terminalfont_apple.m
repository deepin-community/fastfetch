#include "terminalfont.h"
#include "common/font.h"
#include "detection/terminalshell/terminalshell.h"
#include "util/apple/osascript.h"

#include <stdlib.h>
#include <string.h>
#import <Foundation/Foundation.h>

static void detectIterm2(const FFinstance* instance, FFTerminalFontResult* terminalFont)
{
    const char* profile = getenv("ITERM_PROFILE");
    if (profile == NULL)
    {
        ffStrbufAppendS(&terminalFont->error, "environment variable ITERM_PROFILE not set");
        return;
    }

    NSError* error;
    NSString* fileName = [NSString stringWithFormat:@"file://%s/Library/Preferences/com.googlecode.iterm2.plist", instance->state.platform.homeDir.chars];
    NSDictionary* dict = [NSDictionary dictionaryWithContentsOfURL:[NSURL URLWithString:fileName]
                                       error:&error];
    if(error)
    {
        ffStrbufAppendS(&terminalFont->error, error.localizedDescription.UTF8String);
        return;
    }

    for(NSDictionary* bookmark in [dict valueForKey:@"New Bookmarks"])
    {
        if(![[bookmark valueForKey:@"Name"] isEqualToString:@(profile)])
            continue;

        NSString* normalFont = [bookmark valueForKey:@"Normal Font"];
        if(!normalFont)
        {
            ffStrbufAppendF(&terminalFont->error, "`Normal Font` key in profile `%s` doesn't exist", profile);
            return;
        }
        ffFontInitWithSpace(&terminalFont->font, normalFont.UTF8String);
        return;
    }

    ffStrbufAppendF(&terminalFont->error, "find profile `%s` bookmark failed", profile);
}

static void detectAppleTerminal(FFTerminalFontResult* terminalFont)
{
    FF_STRBUF_AUTO_DESTROY font;
    ffStrbufInit(&font);
    ffOsascript("tell application \"Terminal\" to font name of window frontmost & \" \" & font size of window frontmost", &font);

    if(font.length == 0)
    {
        ffStrbufAppendS(&terminalFont->error, "executing osascript failed");
        return;
    }

    ffFontInitWithSpace(&terminalFont->font, font.chars);
}

static void detectWarpTerminal(const FFinstance* instance, FFTerminalFontResult* terminalFont)
{
    NSError* error;
    NSString* fileName = [NSString stringWithFormat:@"file://%s/Library/Preferences/dev.warp.Warp-Stable.plist", instance->state.platform.homeDir.chars];
    NSDictionary* dict = [NSDictionary dictionaryWithContentsOfURL:[NSURL URLWithString:fileName]
                                       error:&error];
    if(error)
    {
        ffStrbufAppendS(&terminalFont->error, error.localizedDescription.UTF8String);
        return;
    }

    NSString* fontName = [dict valueForKey:@"FontName"];
    if(!fontName)
        fontName = @"Hack";
    else
        fontName = [fontName stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"\""]];

    NSString* fontSize = [dict valueForKey:@"FontSize"];
    if(!fontSize)
        fontSize = @"13";

    ffFontInitValues(&terminalFont->font, fontName.UTF8String, fontSize.UTF8String);
}

void ffDetectTerminalFontPlatform(const FFinstance* instance, const FFTerminalShellResult* terminalShell, FFTerminalFontResult* terminalFont)
{
    if(ffStrbufIgnCaseCompS(&terminalShell->terminalProcessName, "iterm.app") == 0 ||
        ffStrbufStartsWithIgnCaseS(&terminalShell->terminalProcessName, "iTermServer-"))
        detectIterm2(instance, terminalFont);
    else if(ffStrbufIgnCaseCompS(&terminalShell->terminalProcessName, "Apple_Terminal") == 0)
        detectAppleTerminal(terminalFont);
    else if(ffStrbufIgnCaseCompS(&terminalShell->terminalProcessName, "WarpTerminal") == 0)
        detectWarpTerminal(instance, terminalFont);
}
