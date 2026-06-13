// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "tico/switch_libnx.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

struct _reent;
typedef struct ui_st UI;
typedef struct ui_string_st UI_STRING;
typedef struct ui_method_st UI_METHOD;

extern "C" {

UI_METHOD* UI_create_method(const char* name);
int UI_method_set_opener(UI_METHOD* method, int (*opener)(UI* ui));
int UI_method_set_writer(UI_METHOD* method, int (*writer)(UI* ui, UI_STRING* uis));
int UI_method_set_reader(UI_METHOD* method, int (*reader)(UI* ui, UI_STRING* uis));
int UI_method_set_closer(UI_METHOD* method, int (*closer)(UI* ui));

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    const off_t original_offset = lseek(fd, 0, SEEK_CUR);
    if (original_offset == static_cast<off_t>(-1)) {
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) == static_cast<off_t>(-1)) {
        return -1;
    }

    const ssize_t result = read(fd, buf, count);
    const int saved_errno = errno;
    lseek(fd, original_offset, SEEK_SET);
    errno = saved_errno;
    return result;
}

uid_t getuid(void) {
    return 0;
}

struct passwd* getpwuid(uid_t) {
    static char name[] = "switch";
    static char home[] = "sdmc:/tico/system/3ds";
    static char shell[] = "";
    static struct passwd entry{};

    entry.pw_name = name;
    entry.pw_uid = 0;
    entry.pw_gid = 0;
    entry.pw_dir = home;
    entry.pw_shell = shell;
    return &entry;
}

long sysconf(int name) {
#ifdef _SC_PAGESIZE
    if (name == _SC_PAGESIZE) {
        return 0x1000;
    }
#endif
#ifdef _SC_PAGE_SIZE
    if (name == _SC_PAGE_SIZE) {
        return 0x1000;
    }
#endif
#ifdef _SC_NPROCESSORS_ONLN
    if (name == _SC_NPROCESSORS_ONLN) {
        return 4;
    }
#endif
#ifdef _SC_PHYS_PAGES
    if (name == _SC_PHYS_PAGES) {
        return 0x100000;
    }
#endif
    errno = EINVAL;
    return -1;
}

int getpagesize(void) {
    return 0x1000;
}

int sigprocmask(int, const sigset_t*, sigset_t* oldset) {
    if (oldset) {
        std::memset(oldset, 0, sizeof(*oldset));
    }
    return 0;
}

int _getentropy_r(struct _reent*, void* buf, size_t len) {
    randomGet(buf, len);
    return 0;
}

static int SwitchUiOpen(UI*) {
    return 1;
}

static int SwitchUiWrite(UI*, UI_STRING*) {
    return 1;
}

static int SwitchUiRead(UI*, UI_STRING*) {
    return 0;
}

static int SwitchUiClose(UI*) {
    return 1;
}

UI_METHOD* UI_OpenSSL(void) {
    static UI_METHOD* method = [] {
        UI_METHOD* created = UI_create_method("Switch null UI");
        if (created) {
            UI_method_set_opener(created, SwitchUiOpen);
            UI_method_set_writer(created, SwitchUiWrite);
            UI_method_set_reader(created, SwitchUiRead);
            UI_method_set_closer(created, SwitchUiClose);
        }
        return created;
    }();
    return method;
}

} // extern "C"
