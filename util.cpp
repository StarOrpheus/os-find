#include "util.h"

#include <cstdio>
#include <cassert>
#include <unistd.h>

bool expect(bool ok,
            std::string const& estr)
{
    if (!ok)
    {
        fprintf(stderr, "%s", estr.c_str());
        assert(false && "Unexpected happend!");
    }

    return ok; // when NDEBUG=true
}

void print_man()
{
    static char const* simple_manpage = "Usage: findutil path [-inum num] [-name name]"
                                      "[-size [-=+]size] [-nlinks nlinks] [-exec path]";

    fprintf(stdout, "%s", simple_manpage);
}

static inline void _fd_wrapper_deleter(int* _fd)
{
    if (_fd == nullptr) return;
    int fd = *_fd;
    if (fd == -1) return;
    close(fd);
}

defered_close defer_close(int* fd)
{
    return defered_close(fd, _fd_wrapper_deleter);
}
