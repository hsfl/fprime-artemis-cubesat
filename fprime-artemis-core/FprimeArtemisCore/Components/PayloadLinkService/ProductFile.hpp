#ifndef ARTEMIS_PAYLOAD_PRODUCT_FILE_HPP
#define ARTEMIS_PAYLOAD_PRODUCT_FILE_HPP
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <sys/stat.h>

namespace ArtemisPayload {
struct Product {
    std::uint32_t id = 0;
    std::uint32_t size = 0;
    std::uint16_t crc = 0;
    std::string path;
};

// The Pi owns complete files. Only one fixed-size staging chunk is read at a
// time, and opening verifies the descriptor before any BEGIN is sent.
class ProductFile {
  public:
    static constexpr std::uint32_t MAX_BYTES = 196608;
    bool open(const Product& product) {
        close();
        if (!product.id || !product.size || product.size > MAX_BYTES || product.path.empty()) return false;
        m_file.open(product.path.c_str(), std::ios::binary);
        if (!m_file) return false;
        std::uint32_t count = 0;
        std::uint16_t crc = 0xFFFF;
        char chunk[200];
        while (m_file) {
            m_file.read(chunk, sizeof(chunk));
            const auto read = m_file.gcount();
            if (read > product.size - count) { close(); return false; }
            count += static_cast<std::uint32_t>(read);
            for (std::streamsize i = 0; i < read; ++i) {
                crc ^= static_cast<std::uint16_t>(static_cast<unsigned char>(chunk[i])) << 8;
                for (unsigned bit = 0; bit < 8; ++bit)
                    crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                                        : static_cast<std::uint16_t>(crc << 1);
            }
        }
        if (!m_file.eof() || count != product.size || crc != product.crc) { close(); return false; }
        m_size = count;
        m_file.clear();
        m_file.seekg(0);
        return true;
    }
    bool read(std::uint32_t offset, std::uint16_t length, unsigned char* destination) {
        if (!m_file.is_open() || !destination || !length || length > 200 ||
            offset > m_size || length > m_size - offset) return false;
        m_file.clear();
        m_file.seekg(offset);
        m_file.read(reinterpret_cast<char*>(destination), length);
        return m_file.gcount() == length;
    }
    void close() {
        if (m_file.is_open()) m_file.close();
        m_file.clear();
        m_size = 0;
    }
  private:
    std::ifstream m_file;
    std::uint32_t m_size = 0;
};

// Last committed product survives a Pi deployment restart. The actual data
// remains in DpWriter's persistent catalog; this is a bounded staging index.
inline bool saveIndex(const std::string& index, const Product& product) {
    if (product.path.find_first_of("\r\n") != std::string::npos) return false;
    const std::string temporary = index + ".tmp";
    std::ofstream out(temporary.c_str(), std::ios::trunc);
    out << "ARTEMIS_PRODUCT_V1\n" << product.id << ' ' << product.size << ' ' << product.crc
        << '\n' << product.path << '\n';
    out.close();
    if (!out) { std::remove(temporary.c_str()); return false; }
    if (std::rename(temporary.c_str(), index.c_str()) != 0) { std::remove(temporary.c_str()); return false; }
    return true;
}

inline bool readIndex(const std::string& index, Product& product) {
    struct stat metadata;
    if (lstat(index.c_str(), &metadata) != 0 || !S_ISREG(metadata.st_mode) || metadata.st_size > 1400) return false;
    std::ifstream in(index.c_str());
    std::string signature, path;
    std::uint64_t id, size, crc;
    if (!std::getline(in, signature) || signature != "ARTEMIS_PRODUCT_V1" || !(in >> id >> size >> crc) ||
        id == 0 || id > std::numeric_limits<std::uint32_t>::max() || size == 0 ||
        size > ProductFile::MAX_BYTES || crc > 0xFFFF) return false;
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    if (!std::getline(in, path) || path.empty()) return false;
    Product candidate;
    candidate.id = static_cast<std::uint32_t>(id);
    candidate.size = static_cast<std::uint32_t>(size);
    candidate.crc = static_cast<std::uint16_t>(crc);
    candidate.path = path;
    product = candidate;
    return true;
}
inline bool loadIndex(const std::string& index, Product& product) {
    Product candidate;
    if (!readIndex(index, candidate)) return false;
    ProductFile file;
    if (!file.open(candidate)) return false;
    product = candidate;
    return true;
}
}
#endif
