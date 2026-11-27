#include "offset_resolver.h"
#include "config.h"
#include "rl_structs.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>

// Minimal JSON helpers (no external dependency)
// Writes/reads {"gnames_rva":"0xHEX","gobjects_rva":"0xHEX"}
namespace {

std::string hex_str(uintptr_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)v);
    return buf;
}

bool parse_hex(const std::string& s, uintptr_t& out) {
    if (s.size() < 3 || s[0] != '0' || s[1] != 'x') return false;
    out = static_cast<uintptr_t>(std::stoull(s, nullptr, 16));
    return true;
}

} // anon

// ── AOB patterns for GNames / GObjects in UE3 Rocket League ─────────────────
// These are well-known patterns from BakkesMod / community RE work.
// Each array ends with the relative offset to the pointer operand.

// GNames: "48 8B 1D ? ? ? ? 48 85 DB 75"
// reads the 32-bit RIP-relative displacement at +3
static const uint8_t PAT_GNAMES[]  = {0x48,0x8B,0x1D,0,0,0,0,0x48,0x85,0xDB,0x75};
static const uint8_t MASK_GNAMES[] = {'x','x','x','?','?','?','?','x','x','x','x'};
static const int     OFF_GNAMES    = 3;  // offset to the 4-byte displacement
static const size_t  LEN_GNAMES    = sizeof(PAT_GNAMES);

// GObjects: "89 0D ? ? ? ? 48 8B DF 48 89 5C 24"
// reads the 32-bit RIP-relative displacement at +2
static const uint8_t PAT_GOBJECTS[]  = {0x89,0x0D,0,0,0,0,0x48,0x8B,0xDF,0x48,0x89,0x5C,0x24};
static const uint8_t MASK_GOBJECTS[] = {'x','x','?','?','?','?','x','x','x','x','x','x','x'};
static const int     OFF_GOBJECTS    = 2;
static const size_t  LEN_GOBJECTS    = sizeof(PAT_GOBJECTS);

// ── resolve ──────────────────────────────────────────────────────────────────
bool OffsetResolver::resolve(const ProcessReader& reader) {
    offsets_ = {};

    // 1. Try hint values first (fast path, <1ms)
    {
        uintptr_t gn_va  = reader.module_base() + Config::GNAMES_RVA_HINT;
        uintptr_t go_va  = reader.module_base() + Config::GOBJECTS_RVA_HINT;
        if (validate_gnames(reader, gn_va) && validate_gobjects(reader, go_va)) {
            offsets_.gnames_rva   = Config::GNAMES_RVA_HINT;
            offsets_.gobjects_rva = Config::GOBJECTS_RVA_HINT;
            offsets_.gnames_va    = gn_va;
            offsets_.gobjects_va  = go_va;
            offsets_.valid        = true;
            std::cout << "[OffsetResolver] Hint offsets validated.\n";
            return true;
        }
    }

    // 2. Try cache
    if (load_cache(reader)) {
        std::cout << "[OffsetResolver] Cache offsets validated.\n";
        return true;
    }

    // 3. AOB scan
    std::cout << "[OffsetResolver] Running AOB scan (~1-2s)...\n";
    auto t0 = std::chrono::steady_clock::now();
    bool ok = aob_scan_for_offsets(reader);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double,std::milli>(t1-t0).count();
    std::cout << "[OffsetResolver] AOB scan " << (ok ? "succeeded" : "FAILED")
              << " in " << ms << "ms\n";

    if (ok) write_cache();
    return ok;
}

// ── validate_gnames ───────────────────────────────────────────────────────────
// GNames is a TStaticIndirectArrayThreadSafeRead<FNameEntry, ...>
// GNames[0] is typically "None", GNames[1] is "ByteProperty"
// We read GNames[1] by: ptr_to_chunk_array → chunk[0] → entry[1]
bool OffsetResolver::validate_gnames(const ProcessReader& reader, uintptr_t gnames_va) const {
    // GNames structure:
    //   GNames        → ptr to outer array (TArray of chunk ptrs)
    //   GNames[0]     → ptr to first chunk (array of FNameEntry*)
    //   chunk[1]      → ptr to FNameEntry for index 1
    //   FNameEntry+8  → the string (after is_wide:1 + index:31 + next ptr)

    uintptr_t outer = 0;
    if (!reader.read(gnames_va, outer) || outer == 0) return false;

    uintptr_t chunk0 = 0;
    if (!reader.read(outer, chunk0) || chunk0 == 0) return false;

    // FNameEntry* for index 1 (second pointer in chunk)
    uintptr_t entry1 = 0;
    if (!reader.read(chunk0 + sizeof(uintptr_t), entry1) || entry1 == 0) return false;

    // FNameEntry layout: { bool is_wide:1 | int32 index:31; FNameEntry* next; char/wchar_t str[]; }
    // string starts at offset 8+8 = 16? varies by build.
    // Try offset 12 (4-byte header + 8-byte next ptr) first, then 8 as fallback.
    for (int str_off : {12, 8, 16}) {
        std::string s = reader.read_string(entry1 + str_off, 16);
        if (s == Config::GNAMES_MAGIC) return true;
    }
    return false;
}

// ── validate_gobjects ─────────────────────────────────────────────────────────
bool OffsetResolver::validate_gobjects(const ProcessReader& reader, uintptr_t gobjects_va) const {
    // GObjects: TChunkedFixedUObjectArray or FUObjectArray
    // First pointer should point to a valid UObject (UObject at index 0 is always "None")
    uintptr_t chunk0 = 0;
    if (!reader.read(gobjects_va, chunk0) || chunk0 == 0) return false;

    uintptr_t obj0 = 0;
    if (!reader.read(chunk0, obj0) || obj0 == 0) return false;

    // obj0 should be in a sane VA range (between 1GB and 1TB on modern Windows)
    return obj0 > 0x10000000ULL && obj0 < 0x10000000000ULL;
}

// ── load_cache ────────────────────────────────────────────────────────────────
bool OffsetResolver::load_cache(const ProcessReader& reader) {
    std::ifstream f(Config::OFFSET_CACHE_PATH);
    if (!f.is_open()) return false;

    std::string line, gn_s, go_s;
    while (std::getline(f, line)) {
        auto get_val = [&](const char* key) -> std::string {
            auto pos = line.find(key);
            if (pos == std::string::npos) return {};
            pos = line.find('"', pos + strlen(key));
            if (pos == std::string::npos) return {};
            pos++;
            auto end = line.find('"', pos);
            return end == std::string::npos ? std::string{} : line.substr(pos, end-pos);
        };
        if (auto v = get_val("gnames_rva");   !v.empty()) gn_s = v;
        if (auto v = get_val("gobjects_rva"); !v.empty()) go_s = v;
    }

    uintptr_t gn_rva = 0, go_rva = 0;
    if (!parse_hex(gn_s, gn_rva) || !parse_hex(go_s, go_rva)) return false;

    uintptr_t gn_va = reader.module_base() + gn_rva;
    uintptr_t go_va = reader.module_base() + go_rva;
    if (!validate_gnames(reader, gn_va) || !validate_gobjects(reader, go_va)) return false;

    offsets_.gnames_rva   = gn_rva;
    offsets_.gobjects_rva = go_rva;
    offsets_.gnames_va    = gn_va;
    offsets_.gobjects_va  = go_va;
    offsets_.valid        = true;
    return true;
}

// ── aob_scan_for_offsets ──────────────────────────────────────────────────────
bool OffsetResolver::aob_scan_for_offsets(const ProcessReader& reader) {
    // Scan GNames
    uintptr_t gn_match = reader.aob_scan_module(PAT_GNAMES, MASK_GNAMES, LEN_GNAMES);
    if (!gn_match) {
        std::cerr << "[OffsetResolver] GNames pattern not found.\n";
        return false;
    }
    // The instruction is RIP-relative: target = match_va + instruction_len + disp32
    // instruction_len for the GNames pattern = 7 bytes (REX + opcode + ModRM + 4-byte disp)
    int32_t gn_disp = 0;
    if (!reader.read(gn_match + OFF_GNAMES, gn_disp)) return false;
    uintptr_t gn_va = gn_match + 7 + static_cast<int64_t>(gn_disp);  // RIP-relative

    if (!validate_gnames(reader, gn_va)) {
        std::cerr << "[OffsetResolver] GNames found at "
                  << std::hex << gn_va << " but failed validation.\n";
        return false;
    }

    // Scan GObjects
    uintptr_t go_match = reader.aob_scan_module(PAT_GOBJECTS, MASK_GOBJECTS, LEN_GOBJECTS);
    if (!go_match) {
        std::cerr << "[OffsetResolver] GObjects pattern not found.\n";
        return false;
    }
    int32_t go_disp = 0;
    if (!reader.read(go_match + OFF_GOBJECTS, go_disp)) return false;
    uintptr_t go_va = go_match + 6 + static_cast<int64_t>(go_disp);

    if (!validate_gobjects(reader, go_va)) {
        std::cerr << "[OffsetResolver] GObjects found but failed validation.\n";
        return false;
    }

    offsets_.gnames_rva   = gn_va  - reader.module_base();
    offsets_.gobjects_rva = go_va  - reader.module_base();
    offsets_.gnames_va    = gn_va;
    offsets_.gobjects_va  = go_va;
    offsets_.valid        = true;

    std::cout << "[OffsetResolver] GNames  RVA: " << std::hex << offsets_.gnames_rva   << "\n";
    std::cout << "[OffsetResolver] GObjects RVA: " << std::hex << offsets_.gobjects_rva << "\n";
    return true;
}

// ── write_cache ───────────────────────────────────────────────────────────────
void OffsetResolver::write_cache() const {
    // Create directory if needed (C++17 would use std::filesystem here)
    system("mkdir config 2>nul");

    std::ofstream f(Config::OFFSET_CACHE_PATH);
    if (!f.is_open()) return;

    f << "{\n"
      << "  \"gnames_rva\":   \"" << hex_str(offsets_.gnames_rva)   << "\",\n"
      << "  \"gobjects_rva\": \"" << hex_str(offsets_.gobjects_rva) << "\"\n"
      << "}\n";
    std::cout << "[OffsetResolver] Cache written to " << Config::OFFSET_CACHE_PATH << "\n";
}

// ── fname_to_string ───────────────────────────────────────────────────────────
std::string OffsetResolver::fname_to_string(
    const ProcessReader& reader, int32_t index) const
{
    if (!offsets_.valid || index < 0) return {};

    // Navigate GNames[chunk][offset_in_chunk]
    // UE3 TStaticIndirectArrayThreadSafeRead: outer is array of chunk ptrs, 0x4000 entries/chunk
    static constexpr int ENTRIES_PER_CHUNK = 0x4000;
    int chunk_idx  = index / ENTRIES_PER_CHUNK;
    int entry_idx  = index % ENTRIES_PER_CHUNK;

    uintptr_t outer = 0;
    if (!reader.read(offsets_.gnames_va, outer) || outer == 0) return {};

    uintptr_t chunk_ptr = 0;
    if (!reader.read(outer + chunk_idx * sizeof(uintptr_t), chunk_ptr) || chunk_ptr == 0) return {};

    uintptr_t entry = 0;
    if (!reader.read(chunk_ptr + entry_idx * sizeof(uintptr_t), entry) || entry == 0) return {};

    // Try string at offset 12 first (most common for this RL build), then 8, 16
    for (int str_off : {12, 8, 16}) {
        std::string s = reader.read_string(entry + str_off, 128);
        if (!s.empty() && s[0] >= 'A' && s[0] <= 'z') return s;
    }
    return {};
}

// ── is_class ─────────────────────────────────────────────────────────────────
bool OffsetResolver::is_class(const ProcessReader& reader,
                              uintptr_t uobject_ptr,
                              const char* class_name) const
{
    if (!uobject_ptr) return false;
    uintptr_t uclass = 0;
    if (!reader.read(uobject_ptr + Config::UOBJECT_CLASS, uclass) || uclass == 0) return false;

    FName class_fname{};
    if (!reader.read(uclass + Config::UOBJECT_NAME, class_fname)) return false;
    if (!class_fname.is_valid()) return false;

    std::string name = fname_to_string(reader, class_fname.index);
    return name == class_name;
}

// ── find_first_of_class ───────────────────────────────────────────────────────
uintptr_t OffsetResolver::find_first_of_class(
    const ProcessReader& reader, const char* class_name) const
{
    auto v = find_all_of_class(reader, class_name, 1);
    return v.empty() ? 0 : v[0];
}

// ── find_all_of_class ─────────────────────────────────────────────────────────
std::vector<uintptr_t> OffsetResolver::find_all_of_class(
    const ProcessReader& reader, const char* class_name, size_t max_count) const
{
    std::vector<uintptr_t> result;
    if (!offsets_.valid) return result;

    // GObjects is a chunked array.
    // Read NumObjects first (at GObjects + 0x10 in some UE3 builds, varies).
    // We iterate up to a safe bound instead to avoid depending on that offset.
    static constexpr int MAX_OBJECTS = 100000;
    static constexpr int CHUNK_SIZE  = 0x10000;

    uintptr_t outer = 0;
    if (!reader.read(offsets_.gobjects_va, outer) || outer == 0) return result;

    for (int chunk_i = 0; chunk_i < 32 && result.size() < max_count; ++chunk_i) {
        uintptr_t chunk_ptr = 0;
        if (!reader.read(outer + chunk_i * sizeof(uintptr_t), chunk_ptr) || chunk_ptr == 0) break;

        for (int entry_i = 0; entry_i < CHUNK_SIZE && result.size() < max_count; ++entry_i) {
            uintptr_t obj_ptr = 0;
            if (!reader.read(chunk_ptr + entry_i * sizeof(uintptr_t), obj_ptr)) continue;
            if (obj_ptr == 0) continue;

            if (is_class(reader, obj_ptr, class_name)) {
                result.push_back(obj_ptr);
            }
        }
    }
    return result;
}
