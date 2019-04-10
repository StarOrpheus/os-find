#include "util.h"

#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <cassert>
#include <cstring>

#include <iostream>
#include <algorithm>
#include <deque>
#include <cstddef>
#include <memory>
#include <unordered_set>
#include <queue>

enum
{
    GETDENTS_BUF_SZ         = 1024,
    EXECVE_ARGS_BUF_SZ      = 1024
};

struct linux_dirent
{
    long           d_ino;
    off_t          d_off;
    unsigned short d_reclen;
    char           d_name[];
};

using argv_vec = std::vector<char*>;

static inline bool parse_argv(findutil_config& config,
                              int argc,
                              char** argv)
{
    if (!expect(argc > 1, "Search path not given!")) return false;
    config.path = argv[1];

    for (int i = 2; i < argc; i += 2)
    {
        if (!expect(i + 1 < argc, "Expected argument after " + std::string(argv[i]))) return false;

        if (strcmp(argv[i], "-inum") == 0)
        {
            config.inode = static_cast<ino_t>(atoll(argv[i + 1]));
            continue;
        }

        if (strcmp(argv[i], "-name") == 0)
        {
            config.name = argv[i + 1];
            continue;
        }

        if (strcmp(argv[i], "-size") == 0)
        {
            auto const& arg = argv[i + 1];
            if (arg[0] >= '0' && arg[0] <= '9')
            {
                config.size = {findutil_config::size_variant::EQ,
                               static_cast<fsize_t>(atoll(arg))};
            }
            else
            {
                switch (arg[0])
                {
                case '-':
                    config.size = {findutil_config::size_variant::LE,
                                   static_cast<fsize_t>(atoll(arg + 1))};
                    break;
                case '=':
                    config.size = {findutil_config::size_variant::EQ,
                                   static_cast<fsize_t>(atoll(arg + 1))};
                    break;
                case '+':
                    config.size = {findutil_config::size_variant::GR,
                                   static_cast<fsize_t>(atoll(arg + 1))};
                    break;
                default:
                    return expect(false, std::string("Unexpected size: ") + arg);
                }
            }
            continue;
        }

        if (strcmp(argv[i], "-nlinks") == 0)
        {
            config.nlinks = static_cast<ino_t>(atoll(argv[i + 1]));
            continue;
        }

        if (strcmp(argv[i], "-exec") == 0)
        {
            config.execp = argv[i + 1];
            continue;
        }

        return expect(false, std::string("Unexpected symbol: ") + argv[i]);
    }

    return expect(config.path.size() > 0, "Path not given!");
}

static inline bool prepare_args(findutil_config const& config,
                                std::string const& path,
                                argv_vec& arg_list)
{
    static char buffer[EXECVE_ARGS_BUF_SZ];
    static char* buff_end;
    buff_end = buffer;

    assert(config.execp);
    if (config.execp->size() + path.size() + 2 > EXECVE_ARGS_BUF_SZ)
    {
        fprintf(stderr, "Length of the path to the executable "
                        "2and the path to the found file is out of buffer size bound.\n");
        return false;
    }

    arg_list.clear();

    strcpy(buff_end, config.execp->c_str());
    buff_end += config.execp->size() + 1;
    arg_list.push_back(buffer);

    strcpy(buff_end, path.c_str());
    arg_list.push_back(buff_end);

    return true;
}

static inline void file_handle(findutil_config& config,
                               std::string const& path,
                               struct linux_dirent* dirent_repr)
{
    if (config.name && config.name != dirent_repr->d_name) return;
    if (config.inode && dirent_repr->d_ino != config.inode) return;

    static struct stat statbuf;
    int stat_ret = stat(path.c_str(), &statbuf);
    if (stat_ret == -1)
    {
        fprintf(stderr, "stat error: %s!\n", strerror(errno));
        return;
    }

    auto const sz = statbuf.st_size;
    auto const nlink = statbuf.st_nlink;

    if (config.size)
    {
        switch (config.size->first)
        {
        case findutil_config::size_variant::LE:
            if (sz >= config.size->second) return;
            break;
        case findutil_config::size_variant::GR:
            if (sz <= config.size->second) return;
            break;
        case findutil_config::size_variant::EQ:
            if (sz != config.size->second) return;
            break;
        default:
            assert(false && "Unexpected size option!");
            return;
        }
    }

    if (config.nlinks && config.nlinks != nlink) return;

    if (config.execp)
    {
        static argv_vec arg_list;

        arg_list.clear();
        bool prep_args_result = prepare_args(config, path, arg_list);
        if (!prep_args_result) return;

        auto current_pid = fork();
        if (current_pid == 0)
        {
            execve(config.execp->c_str(), arg_list.data(), environ);
        } else if (current_pid < 0)
        {
            fprintf(stderr, "%s\n", strerror(errno));
            return;
        } else
        {
            int status;
            auto pd = wait(&status);
            current_pid = 0;
            if (errno == EINTR)
            {
                assert(pd == -1);
                errno = 0;
                fputs("Interruped\n", stderr);
                return;
            }

            if (pd == -1)
            {
                fprintf(stderr, "%s\n", strerror(errno));
                return;
            }

            if (WIFEXITED(status))
            {
                auto exit_code = WEXITSTATUS(status);
                if (exit_code)
                {
                    fprintf(stderr, "Child process exited with code %d\n", exit_code);
                    // TODO: detailed
                }
                return;
            }

            if (WIFSIGNALED(status))
            {
                fprintf(stderr, "Was killed by %s\n", strsignal(WTERMSIG(status)));
                return;
            }
        }
    }
    else
    {
        fprintf(stdout, "%s\n", path.c_str());
        return;
    }
}

int main(int argc, char* argv[])
{
    findutil_config config;
    auto parse_ok = parse_argv(config, argc, argv);
    if (!parse_ok)
    {
        print_man();
        return -1;
    }

    char                    getdents_buffer[GETDENTS_BUF_SZ];
    struct linux_dirent*    dirent;
    std::queue<std::string> q;
    std::string             current_dirent_path;

    q.push(argv[1]);
    while (!q.empty())
    {
        std::string path = std::move(q.front());
        q.pop();

        int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
        defered_close _df1 = defer_close(&fd);
        if (fd == -1)
        {
            fprintf(stderr, "Error opening dir %s: %s\n", path.c_str(), strerror(errno));
            continue;
        }

        for (;;)
        {
            auto nread = syscall(SYS_getdents, fd, getdents_buffer, GETDENTS_BUF_SZ);
            if (nread == -1)
            {
                fprintf(stderr, "Error getting directory contents %s: %s\n", path.c_str(), strerror(errno));
                break;
            }

            if (nread == 0) break;

            for (int bpos = 0; bpos < nread; )
            {
                dirent = reinterpret_cast<struct linux_dirent*>(getdents_buffer + bpos);

                if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
                {
                    bpos += dirent->d_reclen;
                    continue;
                }

                current_dirent_path.clear();
                current_dirent_path += path;
                current_dirent_path += (path.back() == '/' ? "" : "/");
                current_dirent_path += dirent->d_name;

                switch (*(getdents_buffer + bpos + dirent->d_reclen - 1))
                {
                case DT_DIR:
                    q.push(current_dirent_path);
                    break;
                case DT_REG:
                    file_handle(config, current_dirent_path, dirent);
                    break;
                case DT_BLK:
                case DT_CHR:
                case DT_LNK:
                case DT_WHT:
                case DT_FIFO:
                case DT_SOCK:
                case DT_UNKNOWN:
                default:
                    break;
                }

                bpos += dirent->d_reclen;
            }
        }
    }

    return 0;
}
