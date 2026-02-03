#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include <fstream>
#include "hack.h"
#include "zygisk.hpp"
#include "game.h"
#include "log.h"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        auto package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        auto app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        preSpecialize(package_name, app_data_dir);
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (enable_hack) {
            std::thread hack_thread(hack_prepare, game_data_dir, data, length);
            hack_thread.detach();
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool enable_hack;
    char *game_data_dir;
    void *data;
    size_t length;

    static bool module_dir_realpath_from_fd(int dirfd, std::string& out_dir) {
        char linkpath[64];
        snprintf(linkpath, sizeof(linkpath), "/proc/self/fd/%d", dirfd);

        char realdir[PATH_MAX] = {0};
        ssize_t n = readlink(linkpath, realdir, sizeof(realdir) - 1);
        if (n < 0) {
            LOGE("[IL2CPPDUMPER]: readlink(%s) failed: %s (errno=%d)", linkpath, strerror(errno), errno);
            return false;
        }
        realdir[n] = '\0';
        out_dir.assign(realdir);
        return true;
    }

    bool is_target_game(const char *current_process) {
        int dirfd = api->getModuleDir();
        if (dirfd < 0) { LOGE("[IL2CPPDUMPER]:getModuleDir failed"); return false; }

        std::string dirpath;
        if (!module_dir_realpath_from_fd(dirfd, dirpath)) { return false; }

        std::string cfg_path = dirpath + "/il2cpp.cfg";

        std::ifstream cfg(cfg_path);
        if (!cfg.is_open())
        {
            LOGI("[IL2CPPDUMPER]:open fail, path:%s, %s (errno=%d)", cfg_path.c_str(), strerror(errno), errno);
            return false;
        }
        std::string target;
        if (std::getline(cfg, target)) {
            target.erase(0, target.find_first_not_of(" \n\r\t"));
            target.erase(target.find_last_not_of(" \n\r\t") + 1);
            LOGI("[IL2CPPDUMPER]:CurrentName: %s, TargetName: %s, isEqual:%d", current_process, target.c_str(), target == current_process);
            return target == current_process;
        }
        else
        {
            LOGI("[IL2CPPDUMPER]:getline error");
        }
        return false;
    }

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        if (package_name && is_target_game(package_name)) {
            LOGI("detect game: %s", package_name);
            enable_hack = true;
            game_data_dir = new char[strlen(app_data_dir) + 1];
            strcpy(game_data_dir, app_data_dir);

#if defined(__i386__)
            auto path = "zygisk/armeabi-v7a.so";
#endif
#if defined(__x86_64__)
            auto path = "zygisk/arm64-v8a.so";
#endif
#if defined(__i386__) || defined(__x86_64__)
            int dirfd = api->getModuleDir();
            int fd = openat(dirfd, path, O_RDONLY);
            if (fd != -1) {
                struct stat sb{};
                fstat(fd, &sb);
                length = sb.st_size;
                data = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
                close(fd);
            } else {
                LOGW("Unable to open arm file");
            }
#endif
        } else {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
};

REGISTER_ZYGISK_MODULE(MyModule)
