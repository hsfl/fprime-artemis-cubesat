#include "Components/ProductId/ProductId.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Components {
namespace {

constexpr const char* DEFAULT_STORAGE_ROOT = "/home/pi/artemis/data/science-products";
constexpr const char* ID_HIGH_WATER_FILE = "product-id-high-water";
constexpr const char* PRODUCT_PREFIX = "product_";

std::string getProductStorageRoot() {
    const char* configured = std::getenv("N2_STORAGE_ROOT");
    if ((configured != nullptr) && (configured[0] != '\0')) {
        return std::string(configured);
    }
    return std::string(DEFAULT_STORAGE_ROOT);
}

bool ensureDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::string current;
    std::size_t position = 0U;
    if (path[0] == '/') {
        current = "/";
        position = 1U;
    }
    while (position < path.size()) {
        const std::size_t next = path.find('/', position);
        const std::string part = path.substr(position,
                                             (next == std::string::npos) ? std::string::npos : next - position);
        position = (next == std::string::npos) ? path.size() : next + 1U;
        if (part.empty() || (part == ".")) {
            continue;
        }
        if (!current.empty() && (current.back() != '/')) {
            current += "/";
        }
        current += part;
        const std::size_t separator = current.find_last_of('/');
        const std::string parent = (separator == std::string::npos)
            ? std::string(".")
            : ((separator == 0U) ? std::string("/") : current.substr(0U, separator));
        const bool created = (::mkdir(current.c_str(), 0750) == 0);
        if (!created && (errno != EEXIST)) {
            return false;
        }
        struct stat info {};
        if ((::stat(current.c_str(), &info) != 0) || !S_ISDIR(info.st_mode)) {
            return false;
        }
        if (created) {
            const int parentFd = ::open(parent.c_str(), O_RDONLY);
            if ((parentFd < 0) || (::fsync(parentFd) != 0)) {
                if (parentFd >= 0) {
                    (void)::close(parentFd);
                }
                return false;
            }
            (void)::close(parentFd);
        }
    }
    return true;
}

bool readUnsigned(const char*& cursor, U32& value) {
    const char* first = cursor;
    U64 parsed = 0U;
    while (*cursor >= '0' && *cursor <= '9') {
        parsed = parsed * 10U + static_cast<U64>(*cursor - '0');
        if (parsed > std::numeric_limits<U32>::max()) {
            return false;
        }
        ++cursor;
    }
    value = static_cast<U32>(parsed);
    return cursor != first;
}

bool parseProductIdFile(const char* name, U32& productId) {
    if (name == nullptr) {
        return false;
    }
    const char* cursor = name;
    bool acquisitionName = false;
    if (std::strncmp(cursor, "daq_", 4U) == 0) {
        acquisitionName = true;
        cursor += 4U;
        U32 ignored = 0U;
        if (!readUnsigned(cursor, ignored) || std::strncmp(cursor, "_run_", 5U) != 0) {
            return false;
        }
        cursor += 5U;
        if (!readUnsigned(cursor, ignored) || *cursor != '_') {
            return false;
        }
        ++cursor;
    }
    const std::size_t prefixLength = std::strlen(PRODUCT_PREFIX);
    if (std::strncmp(cursor, PRODUCT_PREFIX, prefixLength) != 0) {
        return false;
    }
    cursor += prefixLength;
    U32 parsedId = 0U;
    if (!readUnsigned(cursor, parsedId) || parsedId == 0U) {
        return false;
    }
    if (std::strcmp(cursor, ".bin") != 0 && std::strcmp(cursor, ".csv") != 0 &&
        (acquisitionName || std::strcmp(cursor, ".meta") != 0)) {
        return false;
    }
    productId = parsedId;
    return true;
}

bool readProductIdHighWater(const std::string& path, bool& exists, U32& nextProductId) {
    exists = false;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return errno == ENOENT;
    }
    exists = true;
    char line[64] = {};
    const bool readOk = (std::fgets(line, sizeof(line), file) != nullptr) &&
                        (std::fgetc(file) == EOF) && !std::ferror(file);
    (void)std::fclose(file);
    if (!readOk || (std::strncmp(line, "N2ID1 ", 6U) != 0)) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(line + 6U, &end, 10);
    if ((errno != 0) || (end == line + 6U) || (value > std::numeric_limits<U32>::max())) {
        return false;
    }
    while ((*end == ' ') || (*end == '\t') || (*end == '\r') || (*end == '\n')) {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    nextProductId = static_cast<U32>(value);
    return true;
}

bool writeProductIdHighWater(const std::string& root, U32 nextProductId) {
    const std::string markerPath = root + "/" + ID_HIGH_WATER_FILE;
    char contents[64];
    const int length = std::snprintf(contents, sizeof(contents), "N2ID1 %u\n", nextProductId);
    if ((length <= 0) || (static_cast<std::size_t>(length) >= sizeof(contents))) {
        return false;
    }
    std::string temporaryPath;
    int fd = -1;
    for (U32 attempt = 0U; attempt < 8U; ++attempt) {
        temporaryPath = markerPath + ".tmp." + std::to_string(::getpid()) + "." + std::to_string(attempt);
        fd = ::open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0640);
        if (fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            return false;
        }
    }
    if (fd < 0) {
        return false;
    }
    bool ok = true;
    std::size_t written = 0U;
    while (written < static_cast<std::size_t>(length)) {
        const ssize_t count = ::write(fd, contents + written, static_cast<std::size_t>(length) - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (count == 0) {
            ok = false;
            break;
        }
        written += static_cast<std::size_t>(count);
    }
    ok = ok && (::fsync(fd) == 0);
    if (::close(fd) != 0) {
        ok = false;
    }
    if (!ok || (::rename(temporaryPath.c_str(), markerPath.c_str()) != 0)) {
        (void)::unlink(temporaryPath.c_str());
        return false;
    }
    const int directoryFd = ::open(root.c_str(), O_RDONLY);
    if (directoryFd < 0) {
        return false;
    }
    const bool directorySynced = (::fsync(directoryFd) == 0);
    (void)::close(directoryFd);
    return directorySynced;
}

}  // namespace

bool parseProductIdFilename(const char* name, U32& productIdOut) {
    return parseProductIdFile(name, productIdOut);
}

bool reserveProductId(U32& productIdOut) {
    const std::string root = getProductStorageRoot();
    if (!ensureDirectory(root)) {
        return false;
    }
    const int lockFd = ::open((root + "/product-id.lock").c_str(), O_RDWR | O_CREAT, 0640);
    if (lockFd < 0) {
        return false;
    }
    int lockResult = 0;
    do {
        lockResult = ::flock(lockFd, LOCK_EX);
    } while ((lockResult != 0) && (errno == EINTR));
    if (lockResult != 0) {
        (void)::close(lockFd);
        return false;
    }

    bool markerExists = false;
    U32 markerNext = 1U;
    bool ok = readProductIdHighWater(root + "/" + ID_HIGH_WATER_FILE, markerExists, markerNext);
    U32 highestExistingId = 0U;
    DIR* directory = ok ? ::opendir(root.c_str()) : nullptr;
    if (directory == nullptr) {
        ok = false;
    }
    if (directory != nullptr) {
        struct dirent* entry = nullptr;
        for (;;) {
            errno = 0;
            entry = ::readdir(directory);
            if (entry == nullptr) {
                if (errno != 0) {
                    ok = false;
                }
                break;
            }
            U32 parsedId = 0U;
            if (parseProductIdFile(entry->d_name, parsedId) && (parsedId > highestExistingId)) {
                highestExistingId = parsedId;
            }
        }
        (void)::closedir(directory);
    }
    if (ok && markerExists && (markerNext == 0U)) {
        ok = false;
    }
    if (ok && (highestExistingId == std::numeric_limits<U32>::max())) {
        ok = false;
    }
    U32 nextProductId = markerExists ? markerNext : 1U;
    if (ok && (highestExistingId >= nextProductId)) {
        nextProductId = highestExistingId + 1U;
    }
    if (ok && (nextProductId == 0U)) {
        ok = false;
    }
    if (ok) {
        const U32 followingId = (nextProductId == std::numeric_limits<U32>::max())
            ? 0U
            : nextProductId + 1U;
        ok = writeProductIdHighWater(root, followingId);
        if (ok) {
            productIdOut = nextProductId;
        }
    }
    (void)::flock(lockFd, LOCK_UN);
    (void)::close(lockFd);
    return ok;
}

}  // namespace Components
