// ZaY-o-Nator for your DAW (VST3 and AU). By BeatsbyLonza. MIT licence.
// Built with DPF (DISTRHO Plugin Framework, ISC licence).
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "BeatsbyLonza"
#define DISTRHO_PLUGIN_NAME    "ZaY-o-Nator"
#define DISTRHO_PLUGIN_URI     "https://github.com/zay2001-beatsbylonza/Zay-o-Nator"
#define DISTRHO_PLUGIN_CLAP_ID "com.beatsbylonza.zayonator"

// AU codes (4 characters each): manufacturer, plugin
#define DISTRHO_PLUGIN_BRAND_ID  Bbyl
#define DISTRHO_PLUGIN_UNIQUE_ID ZyNa

#define DISTRHO_PLUGIN_HAS_UI             1
#define DISTRHO_PLUGIN_IS_RT_SAFE         1
#define DISTRHO_PLUGIN_NUM_INPUTS         2
#define DISTRHO_PLUGIN_NUM_OUTPUTS        2
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1   // the screen reads the meters straight from the plugin
#define DISTRHO_UI_USER_RESIZABLE         1
#define DISTRHO_UI_USE_NANOVG             1
#define DISTRHO_UI_DEFAULT_WIDTH          1000
#define DISTRHO_UI_DEFAULT_HEIGHT         620
#define DISTRHO_PLUGIN_VST3_CATEGORIES    "Fx|Analyzer|Stereo"

#endif
