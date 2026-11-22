// A log for where nobody sees the console (the Xbox, a phone): every line goes to stdout or stderr as before and to
// log.txt in the app's data folder (SDL_GetPrefPath; on the Xbox, the package's LocalState, which the Device
// Portal's file explorer can download). The last run's log is kept as log-previous.txt.
#pragma once
#include <string>

namespace amr {

void log_open();                                // at start-up (before anything is logged)
void log_info(const char* fmt, ...);            // stdout + the log
void log_error(const char* fmt, ...);           // stderr + the log
std::string log_path();                         // "" when there is no data folder
// A start-up failure the player must see: the message (and where the log is) in a message box, and in the log.
void fatal(const std::string& message);

}  // namespace amr
