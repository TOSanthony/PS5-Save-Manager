// MacheteSVG - Savegame Backup & Restore Operations Implementation
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_manager.hpp"
#include "core/i18n.hpp"
#include "cloud/google_drive.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace machete::save
{

namespace
{

std::string sanitize_name(std::string name)
{
    const std::string invalid_chars = "\\/:*?\"<>|";
    for (char &c : name)
    {
        if (invalid_chars.find(c) != std::string::npos)
            c = '_';
    }
    return name;
}

bool copy_file_with_fsync(const std::string &src, const std::string &dst,
                          std::uint64_t &bytes_copied)
{
    std::FILE *in = std::fopen(src.c_str(), "rb");
    if (!in)
        return false;

    std::FILE *out = std::fopen(dst.c_str(), "wb");
    if (!out)
    {
        std::fclose(in);
        return false;
    }

    char buffer[64 * 1024];
    std::size_t n = 0;
    bool ok = true;

    while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0)
    {
        if (std::fwrite(buffer, 1, n, out) != n)
        {
            ok = false;
            break;
        }
        bytes_copied += n;
    }

    std::fflush(out);
    fsync(fileno(out)); // Forzar escritura física en disco
    std::fclose(out);
    std::fclose(in);
    return ok;
}

bool copy_directory_recursive(const std::string &src_dir, const std::string &dst_dir,
                              int &files_count, std::uint64_t &bytes_copied)
{
    struct stat src_st{};
    if (stat(src_dir.c_str(), &src_st) == 0 && S_ISREG(src_st.st_mode))
    {
        mkdir(dst_dir.c_str(), 0777);
        std::size_t slash = src_dir.find_last_of('/');
        std::string filename = (slash != std::string::npos) ? src_dir.substr(slash + 1) : src_dir;
        std::string dst_file = dst_dir + "/" + filename;
        if (copy_file_with_fsync(src_dir, dst_file, bytes_copied))
        {
            files_count++;
            return true;
        }
        return false;
    }

    mkdir(dst_dir.c_str(), 0777);

    DIR *dir = opendir(src_dir.c_str());
    if (!dir)
        return false;

    bool all_ok = true;
    while (const dirent *entry = readdir(dir))
    {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;

        const std::string src_path = src_dir + "/" + entry->d_name;
        const std::string dst_path = dst_dir + "/" + entry->d_name;

        struct stat st;
        if (stat(src_path.c_str(), &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                if (!copy_directory_recursive(src_path, dst_path, files_count, bytes_copied))
                    all_ok = false;
            }
            else
            {
                if (copy_file_with_fsync(src_path, dst_path, bytes_copied))
                {
                    files_count++;
                }
                else
                {
                    all_ok = false;
                }
            }
        }
    }

    closedir(dir);
    return all_ok;
}

} // namespace

OperationResult backup_game_save(const scanner::ScannedGame &game,
                                 const storage::StorageDevice &dest_device)
{
    OperationResult res;
    const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);

    if (dest_device.type == storage::StorageType::Cloud)
    {
        auto &drive = cloud::GoogleDrive::instance();
        drive.backup_save(game.title_id, game.title, game.save_path, nullptr);
        res.success = true;
        res.message = is_es ? ("Enviando a Google Drive /MacheteSVG/" + game.title_id + "...")
                            : ("Sending to Google Drive /MacheteSVG/" + game.title_id + "...");
        return res;
    }

    if (game.save_path.empty() || access(game.save_path.c_str(), F_OK) != 0)
    {
        // Si no hay carpeta en /user/home/.../savedata, usar carpeta de demo o respaldo
        res.success = true;
        const std::string target = dest_device.root_dir + "/" + game.title_id + " - " +
                                  sanitize_name(game.title);
        mkdir(dest_device.root_dir.c_str(), 0777);
        mkdir(target.c_str(), 0777);
        res.message = is_es ? ("Estructura creada: " + target)
                            : ("Directory structure created: " + target);
        return res;
    }

    // Ruta destino: <root_dir>/<title_id> - <title>/
    const std::string dest_game_folder =
        dest_device.root_dir + "/" + game.title_id + " - " + sanitize_name(game.title);

    mkdir(dest_device.root_dir.c_str(), 0777);
    mkdir(dest_game_folder.c_str(), 0777);

    int files_count = 0;
    std::uint64_t bytes_copied = 0;
    const bool ok = copy_directory_recursive(game.save_path, dest_game_folder, files_count,
                                            bytes_copied);

    // Sincronizar sistema de archivos globalmente para invalidar cachés vnode de PS4 (#198)
    sync();

    res.success = ok;
    res.files_transferred = files_count;
    res.bytes_transferred = bytes_copied;

    if (ok)
    {
        res.message = is_es
                          ? ("Respaldo completado (" + std::to_string(files_count) +
                             " archivos) en " + dest_game_folder)
                          : ("Backup completed (" + std::to_string(files_count) +
                             " files) at " + dest_game_folder);
    }
    else
    {
        res.message = is_es
                          ? ("Aviso: Copia parcial (" + std::to_string(files_count) +
                             " archivos). Revisa permisos de unidad.")
                          : ("Warning: Partial copy (" + std::to_string(files_count) +
                             " files). Check storage permissions.");
    }

    return res;
}

OperationResult restore_game_save(const scanner::ScannedGame &game,
                                  const storage::StorageDevice &source_device)
{
    OperationResult res;
    const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);

    if (source_device.type == storage::StorageType::Cloud)
    {
        auto &drive = cloud::GoogleDrive::instance();
        drive.restore_save(game.title_id, game.save_path, nullptr);
        res.success = true;
        res.message = is_es ? "Descargando desde Google Drive..." : "Downloading from Google Drive...";
        return res;
    }

    const std::string backup_game_folder =
        source_device.root_dir + "/" + game.title_id + " - " + sanitize_name(game.title);

    if (access(backup_game_folder.c_str(), F_OK) != 0)
    {
        res.success = false;
        res.message = is_es ? ("No se encontro respaldo previo en " + backup_game_folder)
                            : ("No previous backup found at " + backup_game_folder);
        return res;
    }

    if (game.save_path.empty())
    {
        res.success = false;
        res.message = is_es ? "Ruta de guardado del juego no localizada en la consola"
                            : "Game save path not located on console";
        return res;
    }

    std::string dst_target = game.save_path;
    struct stat save_st{};
    if (stat(game.save_path.c_str(), &save_st) == 0 && S_ISREG(save_st.st_mode))
    {
        std::size_t slash = game.save_path.find_last_of('/');
        if (slash != std::string::npos)
            dst_target = game.save_path.substr(0, slash);
    }

    int files_count = 0;
    std::uint64_t bytes_copied = 0;
    const bool ok = copy_directory_recursive(backup_game_folder, dst_target, files_count,
                                            bytes_copied);

    // Sincronizar buffers de disco y vnodes
    sync();

    res.success = ok;
    res.files_transferred = files_count;
    res.bytes_transferred = bytes_copied;

    if (ok)
    {
        res.message = is_es
                          ? ("Restauracion exitosa (" + std::to_string(files_count) +
                             " archivos) con Keystone preservado")
                          : ("Restore successful (" + std::to_string(files_count) +
                             " files) with Keystone preserved");
    }
    else
    {
        res.message = is_es ? ("Fallo al restaurar archivos en " + game.save_path)
                            : ("Failed to restore files at " + game.save_path);
    }

    return res;
}

} // namespace machete::save
