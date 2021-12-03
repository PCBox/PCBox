/*
 * C functionality for Qt platform, where the C equivalent is not easily
 * implemented in Qt
 */

#include <stdint.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/nvr.h>

#include "qt_sdl.h"

int qt_nvr_save(void) {
    return nvr_save();
}

char  icon_set[256] = "";  /* name of the iconset to be used */

wchar_t* plat_get_string(int i)
{
    switch (i)
    {
    case IDS_2077:
        return L"Click to capture mouse.";
    case IDS_2078:
        return L"Press CTRL-END to release mouse";
    case IDS_2079:
        return L"Press CTRL-END or middle button to release mouse";
    case IDS_2080:
        return L"Failed to initialize FluidSynth";
    case IDS_4099:
        return L"MFM/RLL or ESDI CD-ROM drives never existed";
    case IDS_2093:
        return L"Failed to set up PCap";
    case IDS_2094:
        return L"No PCap devices found";
    case IDS_2110:
        return L"Unable to initialize FreeType";
    case IDS_2111:
        return L"Unable to initialize SDL, libsdl2 is required";
    case IDS_2131:
        return L"libfreetype is required for ESC/P printer emulation.";
    case IDS_2132:
        return L"libgs is required for automatic conversion of PostScript files to PDF.\n\nAny documents sent to the generic PostScript printer will be saved as PostScript (.ps) files.";
    case IDS_2129:
        return L"Make sure libpcap is installed and that you are on a libpcap-compatible network connection.";
    case IDS_2114:
        return L"Unable to initialize Ghostscript";
    case IDS_2063:
        return L"Machine \"%hs\" is not available due to missing ROMs in the roms/machines directory. Switching to an available machine.";
    case IDS_2064:
        return L"Video card \"%hs\" is not available due to missing ROMs in the roms/video directory. Switching to an available video card.";
    case IDS_2128:
        return L"Hardware not available";
    }
    return L"";
}

int
plat_vidapi(char* api) {
    return 0;
}

char* plat_vidapi_name(int api) {
    return "default";
}