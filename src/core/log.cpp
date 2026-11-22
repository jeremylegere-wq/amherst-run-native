#include "core/log.h"

#include <SDL.h>

#include <cstdarg>
#include <cstdio>

namespace amr {

namespace {
std::FILE* g_file = nullptr;
std::string g_path;

void write(std::FILE* console, const char* fmt, std::va_list args) {
    char line[2048];
    std::vsnprintf(line, sizeof line, fmt, args);
    std::fputs(line, console);
    std::fputc('\n', console);
    if (g_file) {
        std::fprintf(g_file, "[%8.2f] %s\n", SDL_GetTicks() / 1000.0, line);
        std::fflush(g_file);  // a crash or a console closing the app keeps everything up to here
    }
}
}  // namespace

void log_open() {
    if (g_file) return;
    if (char* dir = SDL_GetPrefPath("AmherstRun", "AmherstRun")) {
        g_path = std::string(dir) + "log.txt";
        const std::string prev = std::string(dir) + "log-previous.txt";
        SDL_free(dir);
        std::remove(prev.c_str());
        std::rename(g_path.c_str(), prev.c_str());
        g_file = std::fopen(g_path.c_str(), "w");
        if (!g_file) g_path.clear();
    }
}

void log_info(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    write(stdout, fmt, args);
    va_end(args);
}

void log_error(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    write(stderr, fmt, args);
    va_end(args);
}

std::string log_path() { return g_path; }

void fatal(const std::string& message) {
    log_error("FATAL: %s", message.c_str());
    const std::string text = message + (g_path.empty() ? "" : "\n\nThe log: " + g_path);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Amherst Run", text.c_str(), nullptr);
}

}  // namespace amr
