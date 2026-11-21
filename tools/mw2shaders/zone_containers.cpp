// Every shader of the fastfiles given, as the input of tools/xenosrecomp's mw2_hlsl: one file per shader, the D3D
// shader container (the object's header, 0x102A11xx, container[+0x04] bytes) followed by the physical part
// (container[+0x08] bytes: literal constants, then the microcode at Shader.physicalOffset), the layout XenosRecomp
// reads. Written as <out>/<ps|vs>_<xxh3 of the blob>.bin (the names mw2_pack expects).
//
// The zone is walked by mw2-recompiled's fastfile reader (launcher/zone, GPL-3.0; built by build_zone_containers.sh),
// so each part is found where the loading rules put it: a scan of the zone for container tags cannot tell which
// program is whose. The program streams before its object; the object's header is at +40 in a pixel shader's, at +872
// in a vertex shader's (mw2-recompiled launcher/zone/shaders.cpp). Vertex shaders are written unbound: the renderer
// applies the declaration's fetch patch itself (g_InputRemap).
//
// Its inputs are zones already inflated (build_mw2mp_shaders.sh: extract_shaders.zone), as the host has no zlib.
//
// usage: zone_containers <out dir> <zone>...
#include "rules.h"
#include "walker.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace {

uint32_t Word(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: zone_containers <out dir> <zone>...\n");
    return 1;
  }
  const std::filesystem::path out = argv[1];
  std::filesystem::create_directories(out);
  const zone::Schema& s = zone::XboxRules();
  auto member = [&](const char* type, const char* name) {
    const zone::MemberInfo* m = s.infos.at(type)->Find(name);
    if (!m) throw zone::Error(std::string("the rules have no ") + type + "::" + name);
    return m;
  };
  const zone::MemberInfo* programs[2] = {member("GfxVertexShaderLoadDef", "physicalPart"),
                                         member("GfxPixelShaderLoadDef", "physicalPart")};
  const zone::MemberInfo* objects[2] = {member("GfxVertexShaderLoadDef", "cachedPart"),
                                        member("GfxPixelShaderLoadDef", "cachedPart")};
  std::set<uint64_t> written;
  int failed = 0;
  for (int i = 2; i < argc; i++) {
    const std::filesystem::path path = argv[i];
    try {
      std::ifstream file(path, std::ios::binary);
      const std::vector<uint8_t> zone((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      if (zone.empty()) {
        throw zone::Error("empty or unreadable");
      }
      struct Span {
        size_t at = 0, bytes = 0;
        bool set = false;
      } program[2];
      size_t found = 0, fresh = 0, bad = 0;
      zone::Walker walker(s, zone.data(), zone.size());
      walker.onData = [&](const zone::MemberInfo& m, const zone::Place& place, size_t bytes) {
        for (int pixel = 0; pixel < 2; pixel++) {
          if (&m == programs[pixel]) {
            program[pixel] = {place.zonePos, bytes, true};
            return;
          }
          if (&m != objects[pixel]) {
            continue;
          }
          const Span p = program[pixel];
          program[pixel].set = false;
          const size_t header = place.zonePos + (pixel ? 40 : 872);
          if (!p.set || header + 12 > place.zonePos + bytes ||
              (Word(zone.data() + header) & 0xFFFFFF00u) != 0x102A1100u) {
            ++bad;
            return;
          }
          const uint32_t virtual_size = Word(zone.data() + header + 4);
          const uint32_t physical_size = Word(zone.data() + header + 8);
          if (header + virtual_size > place.zonePos + bytes || physical_size != p.bytes) {
            ++bad;
            return;
          }
          std::vector<uint8_t> blob(zone.begin() + ptrdiff_t(header), zone.begin() + ptrdiff_t(header + virtual_size));
          blob.insert(blob.end(), zone.begin() + ptrdiff_t(p.at), zone.begin() + ptrdiff_t(p.at + p.bytes));
          ++found;
          const uint64_t hash = XXH3_64bits(blob.data(), blob.size());
          if (!written.insert(hash).second) {
            return;
          }
          ++fresh;
          char name[64];
          std::snprintf(name, sizeof name, "%s_%016llx.bin", pixel ? "ps" : "vs", (unsigned long long)hash);
          if (std::FILE* f = std::fopen((out / name).string().c_str(), "wb")) {
            std::fwrite(blob.data(), 1, blob.size(), f);
            std::fclose(f);
          }
          return;
        }
      };
      walker.Walk();
      std::printf("%s: %zu shaders, %zu new, %zu unreadable\n", path.filename().string().c_str(), found, fresh, bad);
    } catch (const std::exception& e) {
      std::printf("%s: %s\n", path.filename().string().c_str(), e.what());
      failed++;
    }
  }
  std::printf("%zu distinct containers, %d file(s) failed\n", written.size(), failed);
  return failed ? 1 : 0;
}
