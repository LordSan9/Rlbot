#pragma once
#include "process_reader.h"
#include <cstdint>
#include <string>
#include <optional>

// ============================================================
//  Resolves and caches GNames / GObjects RVAs.
//  On first run (or after a game update) it AOB-scans the .data
//  segment to find them, then writes confirmed values to
//  config/offsets.json so subsequent runs skip the scan.
// ============================================================

struct ResolvedOffsets {
    uintptr_t gnames_rva   = 0;   // RVA from module base
    uintptr_t gobjects_rva = 0;
    uintptr_t gnames_va    = 0;   // runtime VA (base + rva)
    uintptr_t gobjects_va  = 0;
    bool      valid        = false;
};

class OffsetResolver {
public:
    // Attempts to resolve GNames and GObjects.
    // 1. Load from cache (config/offsets.json) and validate.
    // 2. If cache invalid, run AOB scan (~1-2s on typical module).
    // 3. Write confirmed values back to cache.
    // Returns false if neither method succeeds.
    bool resolve(const ProcessReader& reader);

    const ResolvedOffsets& offsets() const { return offsets_; }

    // Looks up a name string from GNames by FName index.
    // Returns empty string on failure.
    std::string fname_to_string(const ProcessReader& reader, int32_t index) const;

    // Checks if a UObject's class name matches the given string.
    bool is_class(const ProcessReader& reader,
                  uintptr_t uobject_ptr,
                  const char* class_name) const;

    // Iterates GObjects to find the first object whose class name matches.
    // Returns 0 if not found.
    uintptr_t find_first_of_class(const ProcessReader& reader,
                                  const char* class_name) const;

    // Returns all objects of the given class (up to max_count).
    std::vector<uintptr_t> find_all_of_class(const ProcessReader& reader,
                                             const char* class_name,
                                             size_t max_count = 64) const;

private:
    ResolvedOffsets offsets_;

    bool load_cache(const ProcessReader& reader);
    bool aob_scan_for_offsets(const ProcessReader& reader);
    bool validate_gnames(const ProcessReader& reader, uintptr_t gnames_va) const;
    bool validate_gobjects(const ProcessReader& reader, uintptr_t gobjects_va) const;
    void write_cache() const;
};
