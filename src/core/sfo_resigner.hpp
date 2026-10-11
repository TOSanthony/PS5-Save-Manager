// MacheteSVG - PS4 param.sfo Re-signer and Account ID Activator
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/logger.hpp"
#include "core/sha256_hmac.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace machete::sfo
{

constexpr std::uint32_t SFO_MAGIC = 0x46535000; // "\0PSF"

// Clave HMAC-SHA256 de Sony para la firma del OpenPSID en param.sfo (PS4 / Prospero BC)
inline const std::uint8_t PSID_HMAC_KEY[16] = {
    0x13, 0xD1, 0xDF, 0x06, 0x75, 0xC9, 0xFD, 0x95,
    0x0A, 0x17, 0xE5, 0x64, 0xC2, 0x77, 0x7F, 0x2C};

#pragma pack(push, 1)
struct SfoHeader
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t key_table_offset;
    std::uint32_t data_table_offset;
    std::uint32_t num_entries;
};

struct SfoIndexEntry
{
    std::uint16_t key_offset;
    std::uint16_t param_format;
    std::uint32_t param_length;
    std::uint32_t param_max_length;
    std::uint32_t data_offset;
};

struct SfoParamsField
{
    std::uint32_t unk1;
    std::uint32_t user_id;       // Offset 0x04: Local User ID (ej. 0x105f615a)
    std::uint8_t psid_hmac[32];  // Offset 0x08: HMAC-SHA256 del OpenPSID
    std::uint32_t unk3;          // Offset 0x28
    char title_id_1[16];         // Offset 0x2C: Title ID principal
    char title_id_2[16];         // Offset 0x3C: Copia de Title ID
    std::uint32_t unk4;          // Offset 0x4C
    std::uint8_t chunk[0x3B0];
};
#pragma pack(pop)

struct ResignParams
{
    std::uint32_t target_user_id = 0;   // User ID local de la PS5 (ej. 0x105f615a)
    std::uint64_t target_account_id = 0; // 64-bit Account ID
    const std::uint8_t *psid = nullptr; // 16 bytes OpenPSID si se conoce
};

inline bool resign_param_sfo(const std::string &sfo_path, const ResignParams &params)
{
    std::FILE *in = std::fopen(sfo_path.c_str(), "rb");
    if (!in)
        return false;

    std::fseek(in, 0, SEEK_END);
    const long fsize = std::ftell(in);
    std::fseek(in, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 2 * 1024 * 1024)
    {
        std::fclose(in);
        return false;
    }

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(fsize));
    const std::size_t read_bytes = std::fread(buffer.data(), 1, buffer.size(), in);
    std::fclose(in);

    if (read_bytes != buffer.size() || buffer.size() < sizeof(SfoHeader))
        return false;

    auto *header = reinterpret_cast<SfoHeader *>(buffer.data());
    if (header->magic != SFO_MAGIC)
        return false;

    if (buffer.size() < sizeof(SfoHeader) + header->num_entries * sizeof(SfoIndexEntry))
        return false;

    auto *entries = reinterpret_cast<SfoIndexEntry *>(buffer.data() + sizeof(SfoHeader));
    const char *key_table = reinterpret_cast<const char *>(buffer.data() + header->key_table_offset);
    std::uint8_t *data_table = buffer.data() + header->data_table_offset;

    bool modified = false;

    for (std::uint32_t i = 0; i < header->num_entries; ++i)
    {
        const char *key = key_table + entries[i].key_offset;
        std::uint8_t *val_ptr = data_table + entries[i].data_offset;

        // 1. Modificar ACCOUNT_ID (8 bytes uint64_t)
        if (std::strcmp(key, "ACCOUNT_ID") == 0 && entries[i].param_max_length >= 8)
        {
            if (params.target_account_id != 0)
            {
                std::memcpy(val_ptr, &params.target_account_id, 8);
                modified = true;
                MACHETE_LOG("[SFO]", "Patched ACCOUNT_ID -> 0x%016llx en %s",
                            static_cast<unsigned long long>(params.target_account_id), sfo_path.c_str());
            }
        }

        // 2. Modificar PARAMS (user_id, psid_hmac, title_id_2)
        if (std::strcmp(key, "PARAMS") == 0 && entries[i].param_max_length >= sizeof(SfoParamsField))
        {
            auto *p = reinterpret_cast<SfoParamsField *>(val_ptr);
            if (params.target_user_id != 0)
            {
                p->user_id = params.target_user_id;
                modified = true;
                MACHETE_LOG("[SFO]", "Patched PARAMS.user_id -> 0x%08x en %s",
                            params.target_user_id, sfo_path.c_str());
            }

            // Sincronizar title_id_2 con title_id_1
            std::memcpy(p->title_id_2, p->title_id_1, sizeof(p->title_id_2));

            // Si se suministra OpenPSID, recalcular HMAC de Sony
            if (params.psid != nullptr)
            {
                crypto::hmac_sha256(PSID_HMAC_KEY, sizeof(PSID_HMAC_KEY),
                                    params.psid, 16,
                                    p->psid_hmac);
                modified = true;
            }
            else
            {
                // Dejar en ceros o preservar firma neutral
                std::memset(p->psid_hmac, 0, 32);
                modified = true;
            }
        }
    }

    if (!modified)
        return true;

    // Escribir de vuelta de forma segura
    const std::string tmp_path = sfo_path + ".tmp";
    std::FILE *out = std::fopen(tmp_path.c_str(), "wb");
    if (!out)
        return false;

    const std::size_t written = std::fwrite(buffer.data(), 1, buffer.size(), out);
    std::fflush(out);
    std::fclose(out);

    if (written != buffer.size())
    {
        std::remove(tmp_path.c_str());
        return false;
    }

    return (std::rename(tmp_path.c_str(), sfo_path.c_str()) == 0);
}

} // namespace machete::sfo
