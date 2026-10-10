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
#include <unordered_map>

#if defined(__PROSPERO__) || defined(__Prospero__) || defined(__PS5__)
#include <GL/glcorearb.h>
#endif

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

bool is_valid_title_id(const std::string &id)
{
    if (id.size() != 9)
        return false;
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (id[i] < 'A' || id[i] > 'Z')
            return false;
    }
    for (std::size_t i = 4; i < 9; ++i)
    {
        if (id[i] < '0' || id[i] > '9')
            return false;
    }
    return true;
}

std::uint64_t calculate_dir_size_and_mtime(const std::string &path, std::time_t &out_mtime, int depth = 0)
{
    if (depth > 2)
        return 0;

    struct stat st{};
    if (lstat(path.c_str(), &st) != 0)
        return 0;

    if (S_ISREG(st.st_mode))
    {
        out_mtime = st.st_mtime;
        return static_cast<std::uint64_t>(st.st_size);
    }

    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return 0;

    DIR *dir = opendir(path.c_str());
    if (!dir)
        return 0;

    std::uint64_t total = 0;
    out_mtime = st.st_mtime;

    while (const dirent *entry = readdir(dir))
    {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;

        const std::string full = path + "/" + entry->d_name;
        struct stat entry_st{};
        if (lstat(full.c_str(), &entry_st) == 0)
        {
            if (S_ISDIR(entry_st.st_mode) && !S_ISLNK(entry_st.st_mode))
            {
                std::time_t sub_mtime = 0;
                total += calculate_dir_size_and_mtime(full, sub_mtime, depth + 1);
                if (sub_mtime > out_mtime)
                    out_mtime = sub_mtime;
            }
            else if (S_ISREG(entry_st.st_mode))
            {
                total += static_cast<std::uint64_t>(entry_st.st_size);
                if (entry_st.st_mtime > out_mtime)
                    out_mtime = entry_st.st_mtime;
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

    // Validar que el archivo contenga texto JSON estructurado con llave de apertura '{'
    const auto first_char = content.find_first_not_of(" \t\r\n");
    if (first_char == std::string::npos || content[first_char] != '{')
        return "";

    // 1. Intentar buscar "titleName": "...", "title": "...", etc.
    for (const char *key : {"\"titleName\"", "\"title\"", "\"name\"", "\"titleText\""})
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

    // Leer tabla de claves con proteccion de rango
    if (key_table_offset > 65536 || data_table_offset > 524288 || data_table_offset <= key_table_offset)
    {
        std::fclose(f);
        return "";
    }

    std::fseek(f, static_cast<long>(key_table_offset), SEEK_SET);
    const std::size_t keys_size = static_cast<std::size_t>(data_table_offset - key_table_offset);
    if (keys_size > 32768)
    {
        std::fclose(f);
        return "";
    }

    std::vector<char> keys(keys_size + 1, 0);
    std::fread(keys.data(), 1, keys_size, f);

    // Buscar "TITLE" o "TITLE_" (localizado)
    for (const auto &entry : entries)
    {
        if (entry.key_offset < keys_size)
        {
            const char *key_str = &keys[entry.key_offset];
            if (std::strcmp(key_str, "TITLE") == 0 || std::strncmp(key_str, "TITLE_", 6) == 0)
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
    }

    std::fclose(f);
    return "";
}

std::vector<ScannedGame> scan_installed_games_and_saves()
{
    std::vector<ScannedGame> games;
    std::unordered_map<std::string, std::time_t> latest_mtimes;

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
    // 1. Escanear Metadatos de Aplicaciones Instaladas Reales
    //    En PS5, el directorio canonico de metadatos de titulos instalados es:
    //    /user/appmeta/ (interno) y /mnt/ext0/user/appmeta / /mnt/ext1/user/appmeta (externo)
    //    NOTA CRITICA: NUNCA escanear /user/app (punto de montaje PFS de contenedores cifrados)
    // =========================================================================
    const char *meta_bases[] = {
        "/user/appmeta",
        "/mnt/ext0/user/appmeta",
        "/mnt/ext1/user/appmeta",
        "/data/homebrew",
    };

    for (const char *meta_base : meta_bases)
    {
        DIR *dir = opendir(meta_base);
        if (!dir)
            continue;

        while (const dirent *entry = readdir(dir))
        {
            if (entry->d_name[0] == '.')
                continue;

            const std::string title_id = entry->d_name;
            if (!is_valid_title_id(title_id))
                continue;

            const std::string entry_path = std::string(meta_base) + "/" + title_id;

            auto &game = find_or_add_game(title_id);
            if (game.app_path.empty())
                game.app_path = entry_path;

            // Ruta de icono
            if (game.icon_path.empty())
            {
                for (const std::string &p : {
                         entry_path + "/icon0.png",
                         entry_path + "/sce_sys/icon0.png",
                         std::string("/user/appmeta/") + title_id + "/icon0.png",
                         std::string("/mnt/ext0/user/appmeta/") + title_id + "/icon0.png",
                         std::string("/mnt/ext1/user/appmeta/") + title_id + "/icon0.png",
                     })
                {
                    if (file_exists(p))
                    {
                        game.icon_path = p;
                        break;
                    }
                }
            }

            // Nombre oficial del juego
            if (game.title.empty() || game.title == title_id)
            {
                if (file_exists(entry_path + "/param.json"))
                    game.title = extract_title_from_param_json(entry_path + "/param.json");
                if (game.title.empty() && file_exists(entry_path + "/sce_sys/param.json"))
                    game.title = extract_title_from_param_json(entry_path + "/sce_sys/param.json");
                if (game.title.empty() && file_exists(entry_path + "/param.sfo"))
                    game.title = extract_title_from_param_sfo(entry_path + "/param.sfo");
                if (game.title.empty() && file_exists(entry_path + "/sce_sys/param.sfo"))
                    game.title = extract_title_from_param_sfo(entry_path + "/sce_sys/param.sfo");
                if (game.title.empty() && file_exists(entry_path + "/sce_sys/PARAM.SFO"))
                    game.title = extract_title_from_param_sfo(entry_path + "/sce_sys/PARAM.SFO");
                if (game.title.empty() && file_exists(std::string("/user/appmeta/") + title_id + "/param.json"))
                    game.title = extract_title_from_param_json(std::string("/user/appmeta/") + title_id + "/param.json");
                if (game.title.empty() && file_exists(std::string("/user/appmeta/") + title_id + "/param.sfo"))
                    game.title = extract_title_from_param_sfo(std::string("/user/appmeta/") + title_id + "/param.sfo");
            }

            if (game.size_str.empty())
            {
                game.size_str = "Instalado";
                game.timestamp_str = "Listo para respaldar";
                game.has_keystone = true;
            }
        }
        closedir(dir);
    }

    // =========================================================================
    // 2. Escanear Partidas Guardadas:
    //    - PS5: /user/home/*/savedata_prospero/
    //    - PS4: /user/home/*/savedata/
    //    - Copias: /user/home/*/savedata_backup/
    // =========================================================================
    DIR *home_dir = opendir("/user/home");
    if (home_dir)
    {
        while (const dirent *user_entry = readdir(home_dir))
        {
            if (user_entry->d_name[0] == '.')
                continue;

            const std::string user_base = std::string("/user/home/") + user_entry->d_name;

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

                    const std::string entry_name = game_entry->d_name;
                    if (entry_name.size() < 9)
                        continue;

                    const std::string title_id = entry_name.substr(0, 9);
                    if (!is_valid_title_id(title_id))
                        continue;

                    const std::string game_save_item = savedata_path + "/" + entry_name;

                    auto &game = find_or_add_game(title_id);
                    if (game.save_path.empty())
                        game.save_path = game_save_item;

                    // Si aun no tenia titulo, buscar en metadatos conocidos
                    if (game.title.empty() || game.title == title_id)
                    {
                        for (const char *base : {"/user/appmeta", "/mnt/ext0/user/appmeta", "/mnt/ext1/user/appmeta", "/data/homebrew"})
                        {
                            const std::string p_json = std::string(base) + "/" + title_id + "/param.json";
                            if (file_exists(p_json))
                            {
                                game.title = extract_title_from_param_json(p_json);
                                if (!game.title.empty())
                                    break;
                            }
                            const std::string p_sfo = std::string(base) + "/" + title_id + "/param.sfo";
                            if (file_exists(p_sfo))
                            {
                                game.title = extract_title_from_param_sfo(p_sfo);
                                if (!game.title.empty())
                                    break;
                            }
                        }
                    }

                    // Si no tenia icono, buscar en metadatos conocidos
                    if (game.icon_path.empty())
                    {
                        for (const char *base : {"/user/appmeta", "/mnt/ext0/user/appmeta", "/mnt/ext1/user/appmeta", "/data/homebrew"})
                        {
                            const std::string p_ico = std::string(base) + "/" + title_id + "/icon0.png";
                            if (file_exists(p_ico))
                            {
                                game.icon_path = p_ico;
                                break;
                            }
                        }
                    }

                    game.has_keystone = true;

                    // Acumular tamano total de partidas para este titulo y registrar ultima fecha
                    std::time_t mtime = 0;
                    const std::uint64_t bytes = calculate_dir_size_and_mtime(game_save_item, mtime);
                    if (bytes > 0)
                    {
                        game.total_bytes += bytes;
                        game.size_str = format_bytes(game.total_bytes);
                        if (mtime > latest_mtimes[title_id])
                        {
                            latest_mtimes[title_id] = mtime;
                            game.timestamp_str = format_timestamp(mtime);
                        }
                    }
                }
                closedir(savedata_dir);
            }
        }
        closedir(home_dir);
    }

    // =========================================================================
    // 3. Filtrar entradas fantasma (sin partidas guardadas Y sin metadatos reales)
    // =========================================================================
    std::vector<ScannedGame> valid_games;
    valid_games.reserve(games.size());
    for (auto &game : games)
    {
        const bool has_saves = (game.total_bytes > 0 || !game.save_path.empty());
        const bool has_real_title = (!game.title.empty() && game.title != game.title_id);
        const bool has_icon = (!game.icon_path.empty());

        if (has_saves || has_real_title || has_icon)
        {
            if (game.title.empty())
                game.title = game.title_id;
            if (game.size_str.empty())
                game.size_str = (game.total_bytes > 0) ? format_bytes(game.total_bytes) : "Partida detectada";
            if (game.timestamp_str.empty())
                game.timestamp_str = "Listo para respaldar";
            valid_games.push_back(std::move(game));
        }
    }

    // Ordenar: primero aquellos con partidas guardadas (bytes > 0), luego por titulo
    std::sort(valid_games.begin(), valid_games.end(), [](const ScannedGame &a, const ScannedGame &b) {
        if ((a.total_bytes > 0) != (b.total_bytes > 0))
            return a.total_bytes > b.total_bytes;
        return a.title < b.title;
    });

    return valid_games;
}

void load_game_icon_textures(std::vector<ScannedGame> &games)
{
    // Las tarjetas vectoriales procedurales renderizan con elegancia a 60 FPS
    // sin requerir asignaciones dinámicas en VRAM de AGC
    for (auto &game : games)
        game.icon_texture = 0;
}

void release_game_icon_textures(std::vector<ScannedGame> &games)
{
    for (auto &game : games)
        game.icon_texture = 0;
}

} // namespace machete::scanner
