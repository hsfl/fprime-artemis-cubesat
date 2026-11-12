#ifndef ARTEMIS_PAYLOAD_PRODUCT_CATALOG_HPP
#define ARTEMIS_PAYLOAD_PRODUCT_CATALOG_HPP
#include "ProductFile.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>

namespace ArtemisPayload {
// Fixed-work ID lookup over DpWriter-owned files. Every record originates in a
// successful DpWritten descriptor. Remote input supplies only a numeric ID;
// it never supplies a path or causes a directory scan.
class ProductCatalog {
  public:
    ProductCatalog(const std::string& index, const std::string& root) : m_index(index), m_root(root) {}
    bool latest(Product& product) const {
        Product candidate;
        return readIndex(m_index, candidate) && lookup(candidate.id, product);
    }
    bool lookup(std::uint32_t id, Product& product) const {
        if (!id) return false;
        Product candidate;
        if (!readIndex(record(id), candidate)) {
            // Migrate the previous single-product index without scanning DpCat.
            if (!readIndex(m_index, candidate) || candidate.id != id) return false;
        }
        if (candidate.id != id || !validate(candidate)) return false;
        product = candidate;
        return true;
    }
    bool remember(Product& product) const {
        Product candidate = product;
        if (!validate(candidate) || !ensureRecords()) return false;
        Product previous;
        const bool havePrevious = readIndex(m_index, previous);
        struct stat metadata;
        if (!havePrevious && (lstat(m_index.c_str(), &metadata) == 0 || !recordsEmpty())) return false;
        // Driver-local IDs restart at one. The persistent high-water record
        // provides unique public product handles across Pi deployment restarts.
        const std::uint64_t next = std::max<std::uint64_t>(candidate.id,
            havePrevious ? static_cast<std::uint64_t>(previous.id) + 1 : 1);
        if (next > std::numeric_limits<std::uint32_t>::max()) return false;
        candidate.id = static_cast<std::uint32_t>(next);
        if (lstat(record(candidate.id).c_str(), &metadata) == 0) return false;  // Never overwrite an old identity.
        if (havePrevious && lstat(record(previous.id).c_str(), &metadata) != 0 && validate(previous)) {
            if (!saveIndex(record(previous.id), previous)) return false;
        }
        // Reserve the high-water identity first. A process stop between these
        // atomic renames leaves a recoverable latest record, never an orphan
        // sidecar whose ID could be reassigned after restart.
        if (!saveIndex(m_index, candidate) || !saveIndex(record(candidate.id), candidate)) return false;
        product = candidate;
        return true;
    }
  private:
    std::string record(std::uint32_t id) const { return m_index + ".records/" + std::to_string(id) + ".product"; }
    bool ensureRecords() const {
        const std::string directory = m_index + ".records";
        if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) return false;
        struct stat metadata;
        return lstat(directory.c_str(), &metadata) == 0 && S_ISDIR(metadata.st_mode);
    }
    bool recordsEmpty() const {
        DIR* directory = opendir((m_index + ".records").c_str());
        if (!directory) return errno == ENOENT;
        bool empty = true;
        while (dirent* entry = readdir(directory)) {
            const std::string name(entry->d_name);
            if (name != "." && name != "..") { empty = false; break; }
        }
        closedir(directory);
        return empty;
    }
    bool validate(Product& product) const {
        char root[PATH_MAX], path[PATH_MAX];
        if (!realpath(m_root.c_str(), root) || !realpath(product.path.c_str(), path)) return false;
        const std::string prefix = std::string(root) + "/";
        const std::string canonical(path);
        if (canonical.compare(0, prefix.size(), prefix) != 0) return false;
        struct stat metadata;
        if (stat(path, &metadata) != 0 || !S_ISREG(metadata.st_mode)) return false;
        product.path = canonical;
        ProductFile file;
        return file.open(product);
    }
    const std::string m_index, m_root;
};
}
#endif
