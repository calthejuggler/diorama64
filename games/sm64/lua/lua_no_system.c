#include <errno.h>
#include <stddef.h>
int ar_lua_no_system(const char *cmd) {
    if (cmd == NULL) return 0;
    errno = ENOSYS;
    return -1;
}
