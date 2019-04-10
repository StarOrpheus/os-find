#ifndef FINUTIL_UTIL_H
#define FINUTIL_UTIL_H

#include <sys/stat.h>
#include <memory>
#include <optional>

using fsize_t = decltype (std::declval<struct stat>().st_size);
using link_cnt_t = decltype (std::declval<struct statx>().stx_nlink);

static inline void _fd_wrapper_deleter(int* _fd);
using defered_close = std::unique_ptr<int, decltype(&_fd_wrapper_deleter)>;

struct findutil_config
{
    enum class size_variant
    {
        LE, EQ, GR
    };

    std::string path;
    std::optional<ino64_t> inode;
    std::optional<std::string> name;
    std::optional<std::pair<size_variant, fsize_t>> size;
    std::optional<link_cnt_t> nlinks;
    std::optional<std::string> execp;
};

// helpers
bool            expect(bool ok, std::string const& estr);
void            print_man();
defered_close   defer_close(int* fd);

#endif
