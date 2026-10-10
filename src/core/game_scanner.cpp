// MacheteSVG - Real Game Discovery & Savegame Scanner Implementation
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/game_scanner.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__Prospero__) || defined(__PS5__)
#include <GL/glcorearb.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

namespace machete::scanner
{

namespace
{

std::string format_bytes(std::uint64_t bytes)
{
    char buf[64];
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
    {
        std::snprintf(buf, sizeof(buf), "%.1f GB", mb / 1024.0);
    }
    else if (mb >= 0.1)
    {
        std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%llu KB",
                      static_cast<unsigned long long>(bytes / 1024));
    }
    return std::string(buf);
}

std::string format_timestamp(std::time_t mtime)
{
    char buf[64];
    std::tm *tm_info = std::localtime(&mtime);
    if (!tm_info)
        return "Fecha desconocida";

    std::strftime(buf, sizeof(buf), "%d/%m/%Y, %I:%M %p", tm_info);
    return std::string(buf);
}

std::uint64_t calculate_dir_size_and_mtime(const std::string &path, std::time_t &out_mtime)
{
    std::uint64_t total = 0;
    out_mtime = 0;

    DIR *dir = opendir(path.c_str());
    if (!dir)
        return 0;

    while (const dirent *entry = readdir(dir))
    {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;

        const std::string full = path + "/" + entry->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                std::time_t sub_mtime = 0;
                total += calculate_dir_size_and_mtime(full, sub_mtime);
                if (sub_mtime > out_mtime)
                    out_mtime = sub_mtime;
            }
            else
            {
                total += static_cast<std::uint64_t>(st.st_size);
                if (st.st_mtime > out_mtime)
                    out_mtime = st.st_mtime;
            }
        }
    }
    closedir(dir);
    return total;
}

bool file_exists(const std::string &path)
{
    return access(path.c_str(), F_OK) == 0;
}

} // namespace

std::string extract_title_from_param_json(const std::string &param_path)
{
    std::FILE *file = std::fopen(param_path.c_str(), "rb");
    if (!file)
        return "";

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    if (size <= 0 || size > 512 * 1024)
    {
        std::fclose(file);
        return "";
    }

    std::string content(static_cast<std::size_t>(size), '\0');
    const std::size_t read_bytes = std::fread(content.data(), 1, static_cast<std::size_t>(size), file);
    std::fclose(file);
    content.resize(read_bytes);

    // 1. Intentar buscar "titleName": "..."
    for (const char *key : {"\"titleName\"", "\"title\""})
    {
        std::size_t pos = content.find(key);
        if (pos != std::string::npos)
        {
            pos = content.find(':', pos);
            if (pos != std::string::npos)
            {
                pos = content.find('\"', pos);
                if (pos != std::string::npos)
                {
                    const std::size_t start = pos + 1;
                    const std::size_t end = content.find('\"', start);
                    if (end != std::string::npos && end > start)
                    {
                        return content.substr(start, end - start);
                    }
                }
            }
        }
    }

    return "";
}

std::string extract_title_from_param_sfo(const std::string &sfo_path)
{
    std::FILE *f = std::fopen(sfo_path.c_str(), "rb");
    if (!f)
        return "";

    std::uint8_t header[20];
    if (std::fread(header, 1, sizeof(header), f) != sizeof(header))
    {
        std::fclose(f);
        return "";
    }

    // SFO Magic: "\0PSF" (0x00, 0x50, 0x53, 0x46)
    if (header[0] != 0 || header[1] != 'P' || header[2] != 'S' || header[3] != 'F')
    {
        std::fclose(f);
        return "";
    }

    std::uint32_t key_table_offset = 0;
    std::uint32_t data_table_offset = 0;
    std::uint32_t entries_count = 0;
    std::memcpy(&key_table_offset, &header[8], 4);
    std::memcpy(&data_table_offset, &header[12], 4);
    std::memcpy(&entries_count, &header[16], 4);

    if (entries_count == 0 || entries_count > 256)
    {
        std::fclose(f);
        return "";
    }

    struct SfoEntry
    {
        std::uint16_t key_offset;
        std::uint16_t data_fmt;
        std::uint32_t data_len;
        std::uint32_t data_max_len;
        std::uint32_t data_offset;
    };

    std::vector<SfoEntry> entries(entries_count);
    if (std::fread(entries.data(), sizeof(SfoEntry), entries_count, f) != entries_count)
    {
        std::fclose(f);
        return "";
    }

    // Leer tabla de claves
    std::fseek(f, static_cast<long>(key_table_offset), SEEK_SET);
    const std::size_t keys_size = (data_table_offset > key_table_offset)
                                      ? static_cast<std::size_t>(data_table_offset - key_table_offset)
                                      : 1024;
    std::vector<char> keys(keys_size + 1, 0);
    std::fread(keys.data(), 1, keys_size, f);

    // Buscar "TITLE"
    for (const auto &entry : entries)
    {
        if (entry.key_offset < keys_size && std::strcmp(&keys[entry.key_offset], "TITLE") == 0)
        {
            if (entry.data_len > 0 && entry.data_len < 512)
            {
                std::fseek(f, static_cast<long>(data_table_offset + entry.data_offset), SEEK_SET);
                std::vector<char> val(entry.data_len + 1, 0);
                std::fread(val.data(), 1, entry.data_len, f);
                std::fclose(f);
                return std::string(val.data());
            }
        }
    }

    std::fclose(f);
    return "";
}

std::vector<ScannedGame> scan_installed_games_and_saves()
{
    std::vector<ScannedGame> games;

    auto find_or_add_game = [&](const std::string &title_id) -> ScannedGame &
    {
        for (auto &g : games)
        {
            if (g.title_id == title_id)
                return g;
        }
        ScannedGame new_game;
        new_game.title_id = title_id;
        if (title_id.rfind("PPSA", 0) == 0 || title_id.rfind("PPCX", 0) == 0)
            new_game.platform = "PS5";
        else if (title_id.rfind("CUSA", 0) == 0)
            new_game.platform = "PS4";
        else
            new_game.platform = "PS5";
        games.push_back(std::move(new_game));
        return games.back();
    };

    // =========================================================================
    // 1. Escanear Aplicaciones Instaladas
    // =========================================================================
    const char *app_bases[] = {
        "/user/app",
        "/system_ex/app",
        "/mnt/ext0/user/app",
        "/mnt/ext0/app",
        "/mnt/ext1/user/app",
        "/mnt/ext1/app",
        "/data/homebrew",
    };

    for (const char *app_base : app_bases)
    {
        DIR *app_dir = opendir(app_base);
        if (!app_dir)
            continue;

        while (const dirent *entry = readdir(app_dir))
        {
            if (entry->d_name[0] == '.')
                continue;

            const std::string title_id = entry->d_name;
            const std::string app_path = std::string(app_base) + "/" + title_id;

            auto &game = find_or_add_game(title_id);
            game.app_path = app_path;

            // Buscar icon0.png en rutas oficiales
            if (game.icon_path.empty())
            {
                for (const std::string &p : {
                         app_path + "/sce_sys/icon0.png",
                         app_path + "/icon0.png",
                         std::string("/user/appmeta/") + title_id + "/icon0.png",
                         std::string("/system_data/priv/appmeta/") + title_id + "/icon0.png",
                     })
                {
                    if (file_exists(p))
                    {
                        game.icon_path = p;
                        break;
                    }
                }
            }

            // Buscar título oficial
            if (game.title.empty() || game.title == title_id)
            {
                if (file_exists(app_path + "/sce_sys/param.json"))
                    game.title = extract_title_from_param_json(app_path + "/sce_sys/param.json");
                if (game.title.empty() && file_exists(app_path + "/sce_sys/param.sfo"))
                    game.title = extract_title_from_param_sfo(app_path + "/sce_sys/param.sfo");
                if (game.title.empty() && file_exists(app_path + "/sce_sys/PARAM.SFO"))
                    game.title = extract_title_from_param_sfo(app_path + "/sce_sys/PARAM.SFO");
                if (game.title.empty() && file_exists(std::string("/user/appmeta/") + title_id + "/param.json"))
                    game.title = extract_title_from_param_json(std::string("/user/appmeta/") + title_id + "/param.json");
                if (game.title.empty() && file_exists(std::string("/system_data/priv/appmeta/") + title_id + "/param.json"))
                    game.title = extract_title_from_param_json(std::string("/system_data/priv/appmeta/") + title_id + "/param.json");
            }

            if (game.size_str.empty())
            {
                game.size_str = "Instalado";
                game.timestamp_str = "Listo para respaldar";
                game.has_keystone = true;
            }
        }
        closedir(app_dir);
    }

    // =========================================================================
    // 2. Escanear Partidas Guardadas:
    //    - PS5: /user/home/*/savedata_prospero/
    //    - PS4: /user/home/*/savedata/
    //    - Respaldos /user/home/*/savedata_backup/
    // =========================================================================
    DIR *home_dir = opendir("/user/home");
    if (home_dir)
    {
        while (const dirent *user_entry = readdir(home_dir))
        {
            if (user_entry->d_name[0] == '.')
                continue;

            const std::string user_base = std::string("/user/home/") + user_entry->d_name;

            // Escanear tanto savedata_prospero (PS5) como savedata (PS4) y copias
            for (const char *save_sub : {"savedata_prospero", "savedata", "savedata_backup"})
            {
                const std::string savedata_path = user_base + "/" + save_sub;
                DIR *savedata_dir = opendir(savedata_path.c_str());
                if (!savedata_dir)
                    continue;

                while (const dirent *game_entry = readdir(savedata_dir))
                {
                    if (game_entry->d_name[0] == '.')
                        continue;

                    const std::string title_id = game_entry->d_name;
                    const std::string game_save_folder = savedata_path + "/" + title_id;

                    auto &game = find_or_add_game(title_id);
                    game.save_path = game_save_folder;

                    // Si no tenía icono, buscar en el save o en appmeta
                    if (game.icon_path.empty())
                    {
                        for (const std::string &p : {
                                 game_save_folder + "/sce_sys/icon0.png",
                                 game_save_folder + "/icon0.png",
                                 std::string("/user/appmeta/") + title_id + "/icon0.png",
                                 std::string("/system_data/priv/appmeta/") + title_id + "/icon0.png",
                             })
                        {
                            if (file_exists(p))
                            {
                                game.icon_path = p;
                                break;
                            }
                        }
                    }

                    // Si no tenía título, buscar en el save o en appmeta
                    if (game.title.empty() || game.title == title_id)
                    {
                        if (file_exists(game_save_folder + "/sce_sys/param.json"))
                            game.title = extract_title_from_param_json(game_save_folder + "/sce_sys/param.json");
                        if (game.title.empty() && file_exists(game_save_folder + "/sce_sys/param.sfo"))
                            game.title = extract_title_from_param_sfo(game_save_folder + "/sce_sys/param.sfo");
                        if (game.title.empty() && file_exists(std::string("/user/appmeta/") + title_id + "/param.json"))
                            game.title = extract_title_from_param_json(std::string("/user/appmeta/") + title_id + "/param.json");
                    }

                    // Verificar keystone
                    if (!game.has_keystone)
                    {
                        game.has_keystone = file_exists(game_save_folder + "/sce_sys/keystone") ||
                                            file_exists(game_save_folder + "/keystone");
                    }

                    // Calcular tamaño del guardado y fecha de modificación
                    std::time_t mtime = 0;
                    const std::uint64_t bytes = calculate_dir_size_and_mtime(game_save_folder, mtime);
                    if (bytes > 0)
                    {
                        game.total_bytes = bytes;
                        game.size_str = format_bytes(bytes);
                        game.timestamp_str = format_timestamp(mtime > 0 ? mtime : std::time(nullptr));
                    }
                }
                closedir(savedata_dir);
            }
        }
        closedir(home_dir);
    }

    // =========================================================================
    // 3. Limpieza de nombres por defecto si no tenían param.json/param.sfo
    // =========================================================================
    for (auto &game : games)
    {
        if (game.title.empty())
            game.title = game.title_id;
    }

    // Ordenar: primero aquellos con partidas guardadas (bytes > 0), luego por título
    std::sort(games.begin(), games.end(), [](const ScannedGame &a, const ScannedGame &b) {
        if ((a.total_bytes > 0) != (b.total_bytes > 0))
            return a.total_bytes > b.total_bytes;
        return a.title < b.title;
    });

    return games;
}

void load_game_icon_textures(std::vector<ScannedGame> &games)
{
#if defined(__Prospero__) || defined(__PS5__)
    for (auto &game : games)
    {
        if (game.icon_texture != 0 || game.icon_path.empty())
            continue;

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char *data = stbi_load(game.icon_path.c_str(), &width, &height, &channels, 4);
        if (!data)
            continue;

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        stbi_image_free(data);
        game.icon_texture = static_cast<unsigned int>(tex);
    }
#else
    unsigned int mock_id = 100;
    for (auto &game : games)
    {
        if (!game.icon_path.empty())
            game.icon_texture = ++mock_id;
    }
#endif
}

void release_game_icon_textures(std::vector<ScannedGame> &games)
{
#if defined(__Prospero__) || defined(__PS5__)
    for (auto &game : games)
    {
        if (game.icon_texture != 0)
        {
            GLuint tex = static_cast<GLuint>(game.icon_texture);
            glDeleteTextures(1, &tex);
            game.icon_texture = 0;
        }
    }
#else
    for (auto &game : games)
        game.icon_texture = 0;
#endif
}

} // namespace machete::scanner
