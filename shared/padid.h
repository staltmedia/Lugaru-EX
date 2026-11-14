// LugaruEX - which physical controller a game controller is (GPLv2+)
#pragma once

#include <cctype>
#include <string>

// Steam Input hides a controller and shows a virtual Xbox 360 pad in its place (for
// example when a Steam game starts). Its device path still names the real one:
//   \\.\pipe\HID#VID_045E&PID_028E&IG_00#054C&0CE6&000DAC361BAF768B#0#...
// (vendor & product & the real pad's address). This returns the identity the real pad
// has ("serial:ac-36-1b-af-76-8b"), so it stays the same player; "" when the path names
// no other device.
inline std::string PadIdBehindPath(const char* path)
{
    if (!path) {
        return std::string();
    }
    const std::string p = path;
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find('#', start);
        if (end == std::string::npos) {
            end = p.size();
        }
        const std::string seg = p.substr(start, end - start);
        // XXXX&XXXX&<12..16 hex>
        bool ok = seg.size() >= 10 + 12 && seg.size() <= 10 + 16 && seg[4] == '&' && seg[9] == '&';
        for (size_t i = 0; ok && i < seg.size(); i++) {
            ok = (i == 4 || i == 9) || isxdigit((unsigned char)seg[i]);
        }
        if (ok) {
            const std::string hex = seg.substr(seg.size() - 12);
            if (hex.find_first_not_of('0') != std::string::npos) {
                std::string id = "serial:";
                for (int k = 0; k < 12; k += 2) {
                    if (k) {
                        id += '-';
                    }
                    id += (char)tolower((unsigned char)hex[k]);
                    id += (char)tolower((unsigned char)hex[k + 1]);
                }
                return id;
            }
        }
        start = end + 1;
    }
    return std::string();
}
