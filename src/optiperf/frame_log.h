#pragma once
// A frame-rate log, to measure play sessions (it works in every mode, with every other part off too): each call of
// CSystem::Update on the main thread starts a frame, so the time between two is a frame's time. Every second a line of
// optiperf_frames.csv (next to the log): the seconds since the first frame, that second's frames and frame rate, its
// longest frame, and its frames with the game paused (the menus over a level). At each report a summary in the log: the
// frames counted (not the paused ones, nor those over 1 s: loads, the exit), their average rate, the frame times' median,
// 1 % and 0.1 % lows, the longest, and the share of the time below 100 and 60 fps.
//
// [Debug] FrameLog = off (default) | on   (env OPTIPERF_FRAME_LOG)
//
// Two diagnostics with it (x64; not in the shipped optiperf.ini):
// - The renderer's own log for a few frames, to compare its passes between modes. [Debug] RLogAt = <counted frame>
//   (env OPTIPERF_RLOG_AT): "r_Log <level>" through the console at that frame's start, then "r_Log 0" RLogFrames
//   (OPTIPERF_RLOG_FRAMES, default 2) frames later; RLogLevel (OPTIPERF_RLOG_LEVEL) 1-4 (default 4: every pass, state
//   and shader). The renderer opens and closes Direct3DLog.txt, in the game's folder, at its BeginFrames between.
// - [Debug] PostWatch = <text> (env OPTIPERF_POST_WATCH): the 3D engine's SetPostEffectParam calls for a parameter
//   whose name contains the text are logged (frame, thread, whether a flush ran beside the main thread, caller).
//   CryVision's parameters are read at each frame's start and logged when they change.

#include <cstddef>

namespace optiperf {

void FrameLog_OnModuleLoaded(const wchar_t* baseName, char* base, size_t size);
void FrameLog_Report(const char* when);

}  // namespace optiperf
