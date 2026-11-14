// EasyPSP: brightness of the panel the game is on (as EasyCast 0.2.8).
// g_Config.iEasypspBrightness = % of the panel maximum, 0 = leave the system value.
// Settings > Graphics slider; in game Select + D-Pad Up/Down (virtual keys "EasyPSP Brightness Up/Down"), steps of 10 %.
// run.py applies the saved value at start and restores the system brightness of both panels at exit.
#pragma once
#include <cstdio>
#include <string>
#include "Core/Config.h"
#include "Common/System/OSD.h"

namespace easypsp_bright {

inline std::string panelDir() {
	// game on top (screen 0) = backlight1, bottom (screen 1) = backlight0
	return std::string("/sys/class/backlight/") + (g_Config.iEasypspScreen == 1 ? "backlight0" : "backlight1");
}

inline int readInt(const std::string &path) {
	FILE *f = fopen(path.c_str(), "r");
	if (!f) return -1;
	int v = -1;
	if (fscanf(f, "%d", &v) != 1) v = -1;
	fclose(f);
	return v;
}

inline int readPct() {
	int top = readInt(panelDir() + "/max_brightness"), cur = readInt(panelDir() + "/brightness");
	if (top <= 0 || cur < 0) return 0;
	return (cur * 100 + top / 2) / top;
}

// apply to the panel (pct 5..100); 0 = system value, nothing written
inline void apply(int pct) {
	if (pct < 5) return;
	if (pct > 100) pct = 100;
	int top = readInt(panelDir() + "/max_brightness");
	if (top <= 0) return;
	FILE *f = fopen((panelDir() + "/brightness").c_str(), "w");
	if (!f) return;
	int v = top * pct / 100;
	fprintf(f, "%d", v < 1 ? 1 : v);
	fclose(f);
}

inline void step(int dir) {
	int cur = g_Config.iEasypspBrightness > 0 ? g_Config.iEasypspBrightness : readPct();
	int next = dir > 0 ? (cur / 10 + 1) * 10 : ((cur + 9) / 10 - 1) * 10;
	if (next < 10) next = 10;
	if (next > 100) next = 100;
	g_Config.iEasypspBrightness = next;
	apply(next);
	g_Config.Save("EasypspBrightness");
	char text[64];
	snprintf(text, sizeof(text), "Brightness %d%%", next);
	g_OSD.Show(OSDType::MESSAGE_INFO, text, 1.0f, "easypsp_bright");
}

}  // namespace easypsp_bright
