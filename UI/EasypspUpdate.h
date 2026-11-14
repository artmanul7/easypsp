// EasyPSP: updates from GitHub Releases inside the emulator (Settings > System > EasyPSP), as EasyCast 0.2.8.
// The work is done by the launcher's helper (EASYPSP_UPDATER = runtime/<v>/easypsp_update.py, set by run.py):
// --check / --install print "STEP <text>" progress lines and a last "RESULT <state> <version> <message>" line.
// It runs in a background thread so the menu stays responsive. run.py also runs "--auto" (once a day) at start;
// its result (userdata/update.json) gives a short notice on the main screen when a newer version exists.
#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include "Common/System/OSD.h"

namespace easypsp_update {

struct State {
	std::mutex lock;
	std::string status;   // last STEP / RESULT text for the screen
	std::string result;   // newer | latest | installed | error (empty while running or before the first check)
	std::string version;  // version named by the last RESULT
	std::atomic<bool> busy{ false };
};

inline State &state() { static State s; return s; }
inline const char *tool() { return std::getenv("EASYPSP_UPDATER"); }
inline std::string current() { const char *v = std::getenv("EASYPSP_VERSION"); return v ? v : "?"; }

inline void run(const char *arg) {
	State &s = state();
	if (tool() == nullptr || s.busy.exchange(true))
		return;
	{
		std::lock_guard<std::mutex> g(s.lock);
		s.result.clear();
		s.status = arg[2] == 'c' ? "Checking GitHub..." : "Starting the update...";
	}
	std::thread([arg]() {
		State &s = state();
		std::string cmd = std::string("python3 '") + tool() + "' " + arg + " 2>&1";
		FILE *p = popen(cmd.c_str(), "r");
		char line[512];
		bool got = false;
		while (p != nullptr && std::fgets(line, sizeof(line), p) != nullptr) {
			std::string l(line);
			while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
			std::lock_guard<std::mutex> g(s.lock);
			if (l.rfind("STEP ", 0) == 0) {
				s.status = l.substr(5);
			} else if (l.rfind("RESULT ", 0) == 0) {
				std::string r = l.substr(7);
				size_t a = r.find(' '), b = a == std::string::npos ? a : r.find(' ', a + 1);
				s.result = r.substr(0, a);
				s.version = a == std::string::npos ? "" : r.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
				s.status = b == std::string::npos ? r : r.substr(b + 1);
				got = true;
			}
		}
		if (p != nullptr) pclose(p);
		if (!got) {
			std::lock_guard<std::mutex> g(s.lock);
			s.result = "error";
			s.status = "The update helper did not answer";
		}
		s.busy = false;
	}).detach();
}

inline std::string status() {
	State &s = state();
	std::lock_guard<std::mutex> g(s.lock);
	if (tool() == nullptr) return "Start EasyPSP from its tile to use updates";
	return s.status.empty() ? "Not checked yet" : s.status;
}

inline std::string result() {
	State &s = state();
	std::lock_guard<std::mutex> g(s.lock);
	return s.result;
}

inline std::string version() {
	State &s = state();
	std::lock_guard<std::mutex> g(s.lock);
	return s.version;
}

// once per run: notice on the main screen when the launcher's daily check found a newer version
inline void notifyNewer() {
	static bool done = false;
	if (done) return;
	done = true;
	const char *root = std::getenv("EASYPSP_ROOT");
	if (root == nullptr) return;
	std::string path = std::string(root) + "/userdata/update.json";
	FILE *f = std::fopen(path.c_str(), "r");
	if (f == nullptr) return;
	char buf[512] = {};
	size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
	std::fclose(f);
	std::string j(buf, n);
	if (j.find("\"newer\": true") == std::string::npos) return;
	size_t k = j.find("\"latest\": \"");
	if (k == std::string::npos) return;
	k += 11;
	std::string ver = j.substr(k, j.find('"', k) - k);
	g_OSD.Show(OSDType::MESSAGE_INFO, "EasyPSP " + ver + " is available", "Settings > System > EasyPSP > Install", 6.0f);
}

}  // namespace easypsp_update
