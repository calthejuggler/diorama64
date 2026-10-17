// The run log: Documents/sm64ar.log plus stderr. Scripts wait on its AR_* lines.
#pragma once
namespace ar {
void vos_log_open(const char *path);
void vos_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   // one whole line, flushed; any thread
// Quit: flushes the run log (fsync) and stdio, then ends the process with _exit(code): no atexit
// handlers and no static destructors, so nothing the audio engine, Metal or the game threads still use is destroyed under
// them. Save everything else first.
[[noreturn]] void vos_exit_now(int code);
}
