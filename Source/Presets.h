#pragma once
// POLOOYX — factory presets. Values are in each parameter's natural units; anything not listed
// falls back to its default. These are starting points to be tuned against real vocals.

#include <vector>
#include <utility>

namespace plx
{
struct FactoryPreset
{
    const char* name;
    const char* category;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> p = {
        // ---------------------------------------------------------------- CORE
        { "POLOOYX DEFAULT", "CORE", {} },
        { "POLOOYX CLEAN", "CORE", { {"aura",20}, {"glitch",0}, {"space",20}, {"dark",20}, {"chaos",0}, {"body",45}, {"satDrive",12}, {"satMix",40}, {"shadow",15}, {"microPitch",8} } },
        { "POLOOYX DARK", "CORE", { {"aura",15}, {"glitch",10}, {"space",45}, {"dark",78}, {"chaos",10}, {"body",60}, {"satMode",0}, {"revDamp",75} } },
        { "POLOOYX HARD", "CORE", { {"aura",25}, {"glitch",20}, {"space",25}, {"dark",45}, {"chaos",18}, {"body",75}, {"satMode",2}, {"satDrive",45}, {"retune",0}, {"tuneAmount",100} } },
        { "POLOOYX SPACE", "CORE", { {"aura",55}, {"glitch",10}, {"space",80}, {"dark",40}, {"chaos",15}, {"body",40}, {"dlyDiv",5}, {"duck",70} } },

        // ---------------------------------------------------------------- GLITCH
        { "DIGITAL TEAR", "GLITCH", { {"glitch",65}, {"chaos",35}, {"space",30}, {"dark",35}, {"satMode",2}, {"satDrive",40}, {"glitchRate",3}, {"glitchGate",70} } },
        { "BUFFER DREAM", "GLITCH", { {"glitch",55}, {"chaos",20}, {"space",60}, {"aura",45}, {"glitchRate",2}, {"glitchReverse",80}, {"glitchStutter",50} } },
        { "GLITCHED", "GLITCH", { {"glitch",75}, {"chaos",30}, {"space",25}, {"dark",40}, {"glitchRate",3}, {"glitchPitch",50} } },
        { "BROKEN SIGNAL", "GLITCH", { {"glitch",70}, {"chaos",60}, {"dark",55}, {"satMode",3}, {"satDrive",45}, {"glitchTape",70} } },
        { "ERROR", "GLITCH", { {"glitch",90}, {"chaos",55}, {"space",20}, {"satMode",3}, {"satDrive",60}, {"glitchRate",4}, {"glitchSmart",0} } },

        // ---------------------------------------------------------------- PSYCHEDELIC
        { "ACID DREAM", "PSYCHEDELIC", { {"aura",70}, {"chaos",45}, {"space",60}, {"dark",30}, {"satMode",4}, {"satDrive",30}, {"microPitch",45} } },
        { "VOID", "PSYCHEDELIC", { {"aura",40}, {"space",95}, {"dark",80}, {"chaos",20}, {"glitch",10}, {"revDecay",6}, {"formant",-1.5f} } },
        { "TRIP", "PSYCHEDELIC", { {"aura",60}, {"chaos",60}, {"space",55}, {"glitch",25}, {"movement",60} } },
        { "FLOAT", "PSYCHEDELIC", { {"aura",80}, {"space",75}, {"dark",20}, {"chaos",15}, {"body",30}, {"retune",45} } },
        { "OUT OF BODY", "PSYCHEDELIC", { {"aura",65}, {"space",85}, {"chaos",40}, {"dark",55}, {"formant",2.5f}, {"microPitch",55} } },

        // ---------------------------------------------------------------- VOCAL
        { "NIGHT VOCAL", "VOCAL", { {"aura",35}, {"space",40}, {"dark",60}, {"chaos",5}, {"body",60}, {"glitch",5} } },
        { "UNDERGROUND", "VOCAL", { {"aura",25}, {"space",30}, {"dark",55}, {"chaos",12}, {"body",70}, {"glitch",15}, {"satMode",1}, {"satDrive",35} } },
        { "DARK VOCAL", "VOCAL", { {"aura",15}, {"space",35}, {"dark",85}, {"body",65}, {"glitch",5}, {"formant",-1.0f} } },
        { "WIDE VOCAL", "VOCAL", { {"aura",75}, {"space",45}, {"dark",30}, {"body",50}, {"width",160}, {"microPitch",40} } },
        { "DISTORTED VOCAL", "VOCAL", { {"aura",25}, {"space",25}, {"dark",45}, {"chaos",25}, {"body",65}, {"satMode",2}, {"satDrive",65}, {"satMix",80} } },

        // ---------------------------------------------------------------- EXTREME
        { "SYSTEM FAILURE", "EXTREME", { {"glitch",95}, {"chaos",85}, {"dark",60}, {"space",40}, {"satMode",3}, {"satDrive",75}, {"glitchSmart",0} } },
        { "ALIEN", "EXTREME", { {"aura",60}, {"chaos",70}, {"space",55}, {"dark",40}, {"satMode",4}, {"satDrive",60}, {"formant",5.0f} } },
        { "CORRUPTED", "EXTREME", { {"glitch",85}, {"chaos",70}, {"dark",70}, {"satMode",3}, {"satDrive",85}, {"glitchPitch",80} } },
        { "MELTDOWN", "EXTREME", { {"glitch",60}, {"chaos",95}, {"space",70}, {"dark",60}, {"satMode",4}, {"satDrive",80}, {"glitchTape",100} } },
        { "DEAD SIGNAL", "EXTREME", { {"glitch",70}, {"chaos",50}, {"space",85}, {"dark",95}, {"body",80}, {"formant",-4.0f}, {"satMode",3}, {"satDrive",55} } },
    };
    return p;
}
} // namespace plx
