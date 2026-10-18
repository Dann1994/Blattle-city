#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace app {

bool Config::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = line.substr(0, eq);
        const int value = std::atoi(line.substr(eq + 1).c_str());
        int p = 0, a = 0;
        if (std::sscanf(name.c_str(), "key.%d.%d", &p, &a) == 2) {
            if (p >= 0 && p < 4 && a >= 0 && a < kActions) key[p][a] = value;
        } else if (name == "pad.fire") padFire = value;
        else if (name == "pad.fire2") padFire2 = value;
        else if (name == "pad.cancel") padCancel = value;
        else if (name == "pad.start") padStart = value;
        else if (name == "version") version = value;
        else if (name == "hiscore") hiScore = value;
        else if (name == "vibration") vibration = value != 0;
        else if (name == "fullscreen") fullscreen = value != 0;
        else if (name == "turbo") turbo = value != 0;
        else if (name == "playername") playerName = line.substr(eq + 1);
        else if (name == "friendlyfire") friendlyFire = value < 0 ? 0 : (value > 2 ? 2 : value);
        else if (name == "samples") useSamples = value != 0;
    }
    return true;
}

bool Config::Save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    for (int p = 0; p < 4; ++p)
        for (int a = 0; a < kActions; ++a) f << "key." << p << "." << a << "=" << key[p][a] << "\n";
    f << "version=2" << "\n";
    f << "pad.fire=" << padFire << "\n";
    f << "pad.fire2=" << padFire2 << "\n";
    f << "pad.cancel=" << padCancel << "\n";
    f << "pad.start=" << padStart << "\n";
    f << "vibration=" << (vibration ? 1 : 0) << "\n";
    f << "fullscreen=" << (fullscreen ? 1 : 0) << "\n";
    f << "turbo=" << (turbo ? 1 : 0) << "\n";
    f << "playername=" << playerName << "\n";
    f << "friendlyfire=" << friendlyFire << "\n";
    f << "hiscore=" << hiScore << "\n";
    f << "samples=" << (useSamples ? 1 : 0) << "\n";
    return static_cast<bool>(f);
}

} // namespace app
