#include "../ProductFile.hpp"
#include "../ProductCatalog.hpp"
#include "../../PayloadDriver_Lepton/CaptureWindow.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
#include <unistd.h>

static std::uint16_t crc16(const std::vector<unsigned char>& data) {
    std::uint16_t crc = 0xFFFF;
    for (auto value : data) {
        crc ^= static_cast<std::uint16_t>(value) << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = crc & 0x8000 ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                               : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

int main() {
    Components::CaptureWindow window;
    assert(!window.start(0, 0));
    assert(!window.start(0, 3601));
    assert(!window.start(std::numeric_limits<std::uint64_t>::max(), 1));
    assert(window.start(1000, 3));
    assert(window.active());
    assert(!window.start(1001, 1));
    assert(!window.elapsed(3999));
    assert(window.elapsed(4000));
    window.finish();
    assert(!window.active());
    assert(window.start(100000, 3600));
    assert(!window.elapsed(3699999));
    assert(window.elapsed(3700000));

    char temp[] = "/tmp/artemis-payload-test-XXXXXX";
    assert(mkdtemp(temp));
    const std::string directory(temp), path = directory + "/recorded product.fdp", index = directory + "/index";
    std::vector<unsigned char> original(ArtemisPayload::ProductFile::MAX_BYTES);
    for (std::size_t i = 0; i < original.size(); ++i) original[i] = static_cast<unsigned char>(i * 17 + 3);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(original.data()), original.size()); out.close();
    ArtemisPayload::Product product;
    product.id = 42; product.size = original.size(); product.crc = crc16(original); product.path = path;
    ArtemisPayload::ProductFile file;
    assert(file.open(product));
    unsigned char chunk[200];
    std::vector<unsigned char> reconstructed;
    for (std::uint32_t offset = 0; offset < product.size;) {
        const auto count = static_cast<std::uint16_t>(std::min<std::uint32_t>(200, product.size - offset));
        assert(file.read(offset, count, chunk));
        reconstructed.insert(reconstructed.end(), chunk, chunk + count);
        offset += count;
    }
    assert(reconstructed == original);
    assert(!file.read(product.size, 1, chunk));
    assert(!file.read(0, 201, chunk));
    assert(!file.read(0, 0, chunk));
    assert(!file.read(0, 1, nullptr));
    assert(file.read(0, 200, chunk));  // Re-reading for recovery is independent of stream EOF.
    auto bad = product; bad.crc ^= 1; assert(!file.open(bad));
    bad = product; --bad.size; assert(!file.open(bad));
    bad = product; ++bad.size; assert(!file.open(bad));
    bad = product; bad.path += ".missing"; assert(!file.open(bad));
    bad = product; bad.id = 0; assert(!file.open(bad));
    assert(ArtemisPayload::saveIndex(index, product));
    ArtemisPayload::Product restored;
    assert(ArtemisPayload::loadIndex(index, restored));
    assert(restored.id == product.id && restored.size == product.size && restored.crc == product.crc && restored.path == path);
    bad = product; bad.path += "\ninvalid"; assert(!ArtemisPayload::saveIndex(index, bad));
    { std::ofstream corrupt(index); corrupt << "ARTEMIS_PRODUCT_V1\n42 999999999999 1\n" << path << '\n'; }
    assert(!ArtemisPayload::loadIndex(index, restored));
    { std::ofstream corrupt(index); corrupt << "OLD_VERSION\n42 1 1\n" << path << '\n'; }
    assert(!ArtemisPayload::loadIndex(index, restored));
    assert(ArtemisPayload::saveIndex(index, product));
    // Persistent lookup is keyed by numeric IDs, across restarts and newer products.
    ArtemisPayload::ProductCatalog catalog(index, directory);
    assert(catalog.remember(product));
    assert(product.id == 43);  // Legacy latest ID42 is migrated before allocating.
    const std::string secondPath = directory + "/second.fdp";
    { std::ofstream second(secondPath); second << "123456789"; }
    ArtemisPayload::Product second;
    second.id = 1; second.size = 9; second.crc = 0x29B1; second.path = secondPath;
    assert(catalog.remember(second) && second.id == 44);
    assert(catalog.lookup(42, restored) && restored.size == product.size);  // Older original descriptor.
    assert(catalog.lookup(43, restored) && restored.size == ArtemisPayload::ProductFile::MAX_BYTES);
    assert(catalog.lookup(44, restored) && restored.size == 9);
    assert(!catalog.lookup(0, restored) && !catalog.lookup(999, restored));
    ArtemisPayload::ProductCatalog restarted(index, directory);
    assert(restarted.latest(restored) && restored.id == 44);
    assert(restarted.lookup(42, restored));
    // Directory escape through a forged local descriptor or symlink is refused.
    const std::string outsidePath = directory + ".outside";
    { std::ofstream outside(outsidePath); outside << "123456789"; }
    auto escaped = second; escaped.path = outsidePath;
    assert(!catalog.remember(escaped));
    const std::string link = directory + "/escape.fdp";
    assert(symlink(outsidePath.c_str(), link.c_str()) == 0);
    escaped.path = link; assert(!catalog.remember(escaped));
    std::remove(link.c_str()); std::remove(outsidePath.c_str());
    // A deleted latest FDP does not reset the high-water ID or hide older files.
    std::remove(secondPath.c_str());
    assert(!restarted.latest(restored));
    assert(restarted.lookup(42, restored));
    second.id = 1; second.path = path; second.size = product.size; second.crc = product.crc;
    assert(restarted.remember(second) && second.id == 45);
    { std::ofstream changed(path, std::ios::binary | std::ios::app); changed.put('x'); }
    assert(!catalog.lookup(42, restored));
    assert(!ArtemisPayload::loadIndex(index, restored));  // Modified source is never advertised.
    for (unsigned id = 42; id <= 45; ++id)
        std::remove((index + ".records/" + std::to_string(id) + ".product").c_str());
    rmdir((index + ".records").c_str());
    std::remove(path.c_str()); std::remove(index.c_str()); rmdir(directory.c_str());
    std::cout << "PASS: capture timing, full-size reads, CRC/size failures, older/restarted catalog, stable IDs, path confinement\n";
}
