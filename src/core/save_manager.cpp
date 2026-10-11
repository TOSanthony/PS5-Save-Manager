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

std::uint64_t calculate_directory_size(const std::string &path)
{
    struct stat st{};
    if (stat(path.c_str(), &st) != 0)
        return 0;
    if (S_ISREG(st.st_mode))
        return static_cast<std::uint64_t>(st.st_size);

    DIR *dir = opendir(path.c_str());
    if (!dir)
        return 0;

    std::uint64_t total = 0;
    while (const dirent *entry = readdir(dir))
    {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        const std::string sub_path = path + "/" + entry->d_name;
        struct stat sub_st{};
        if (stat(sub_path.c_str(), &sub_st) == 0)
        {
            if (S_ISDIR(sub_st.st_mode))
                total += calculate_directory_size(sub_path);
            else if (S_ISREG(sub_st.st_mode))
                total += static_cast<std::uint64_t>(sub_st.st_size);
        }
    }
    closedir(dir);
    return total;
}

bool copy_file_with_fsync(const std::string &src, const std::string &dst,
                          std::uint64_t &bytes_copied,
                          std::uint64_t total_bytes,
                          int files_count,
                          const ProgressCallback &progress_cb)
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
        if (progress_cb)
        {
            OperationProgress p;
            p.bytes_transferred = bytes_copied;
            p.total_bytes = total_bytes;
            p.files_transferred = files_count;
            p.progress = (total_bytes > 0)
                ? std::clamp(static_cast<float>(bytes_copied) / static_cast<float>(total_bytes), 0.0f, 0.98f)
                : 0.5f;
            const std::size_t slash = src.find_last_of('/');
            p.current_file = (slash != std::string::npos) ? src.substr(slash + 1) : src;
            progress_cb(p);
        }
    }

    std::fflush(out);
    fsync(fileno(out)); // Forzar escritura física en disco
    std::fclose(out);
    std::fclose(in);
    return ok;
}

bool copy_directory_recursive(const std::string &src_dir, const std::string &dst_dir,
                              int &files_count, std::uint64_t &bytes_copied,
                              std::uint64_t total_bytes,
                              const ProgressCallback &progress_cb)
{
    struct stat src_st{};
    if (stat(src_dir.c_str(), &src_st) == 0 && S_ISREG(src_st.st_mode))
    {
        mkdir(dst_dir.c_str(), 0777);
        chmod(dst_dir.c_str(), 0777);
        std::size_t slash = src_dir.find_last_of('/');
        std::string filename = (slash != std::string::npos) ? src_dir.substr(slash + 1) : src_dir;
        std::string dst_file = dst_dir + "/" + filename;
        if (copy_file_with_fsync(src_dir, dst_file, bytes_copied, total_bytes, files_count, progress_cb))
        {
            files_count++;
            return true;
        }
        return false;
    }

    mkdir(dst_dir.c_str(), 0777);
    chmod(dst_dir.c_str(), 0777);

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
                if (!copy_directory_recursive(src_path, dst_path, files_count, bytes_copied, total_bytes, progress_cb))
                    all_ok = false;
            }
            else
            {
                if (copy_file_with_fsync(src_path, dst_path, bytes_copied, total_bytes, files_count, progress_cb))
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
                                 const storage::StorageDevice &dest_device,
                                 ProgressCallback progress_cb)
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

    std::string root_dir = dest_device.root_dir;
    if (root_dir.empty())
    {
        if (dest_device.type == storage::StorageType::Internal)
            root_dir = "/data/" + std::string(storage::ROOT_DIR_NAME);
        else if (dest_device.type == storage::StorageType::SSD)
            root_dir = "/mnt/ext1/" + std::string(storage::ROOT_DIR_NAME);
        else if (dest_device.type == storage::StorageType::USB)
            root_dir = "/mnt/usb0/" + std::string(storage::ROOT_DIR_NAME);
    }

    if (game.save_path.empty() || access(game.save_path.c_str(), F_OK) != 0)
    {
        // Si no hay carpeta en /user/home/.../savedata, usar carpeta de demo o respaldo
        if (progress_cb)
        {
            OperationProgress p;
            p.files_transferred = 1;
            p.bytes_transferred = 0;
            p.total_bytes = 0;
            p.progress = 1.0f;
            p.current_file = sanitize_name(game.title);
            progress_cb(p);
        }
        res.success = true;
        const std::string target = root_dir + "/" + game.title_id + " - " +
                                  sanitize_name(game.title);
        mkdir(root_dir.c_str(), 0777);
        chmod(root_dir.c_str(), 0777);
        mkdir(target.c_str(), 0777);
        chmod(target.c_str(), 0777);
        res.message = is_es ? ("Estructura creada: " + target)
                            : ("Directory structure created: " + target);
        return res;
    }

    // Ruta destino: <root_dir>/<title_id> - <title>/
    const std::string dest_game_folder =
        root_dir + "/" + game.title_id + " - " + sanitize_name(game.title);

    mkdir(root_dir.c_str(), 0777);
    chmod(root_dir.c_str(), 0777);
    mkdir(dest_game_folder.c_str(), 0777);
    chmod(dest_game_folder.c_str(), 0777);

    std::uint64_t total_bytes = game.total_bytes;
    if (total_bytes == 0)
        total_bytes = calculate_directory_size(game.save_path);

    int files_count = 0;
    std::uint64_t bytes_copied = 0;
    const bool ok = copy_directory_recursive(game.save_path, dest_game_folder, files_count,
                                             bytes_copied, total_bytes, progress_cb);

    if (progress_cb)
    {
        OperationProgress p;
        p.bytes_transferred = bytes_copied;
        p.total_bytes = total_bytes > 0 ? total_bytes : bytes_copied;
        p.files_transferred = files_count;
        p.progress = 0.99f;
        p.current_file = is_es ? "Sincronizando vnodes de disco..." : "Syncing vnode caches...";
        progress_cb(p);
    }

    // Sincronizar sistema de archivos globalmente para invalidar cachés vnode de PS4 (#198)
    sync();

    if (progress_cb)
    {
        OperationProgress p;
        p.bytes_transferred = bytes_copied;
        p.total_bytes = total_bytes > 0 ? total_bytes : bytes_copied;
        p.files_transferred = files_count;
        p.progress = 1.0f;
        p.current_file = is_es ? "Completado" : "Completed";
        progress_cb(p);
    }

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
                                  const storage::StorageDevice &source_device,
                                  ProgressCallback progress_cb)
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

    // Buscar carpeta de respaldo existente (soporta coincidencia exacta o con nombre del juego)
    std::string backup_game_folder =
        source_device.root_dir + "/" + game.title_id + " - " + sanitize_name(game.title);

    if (access(backup_game_folder.c_str(), F_OK) != 0)
    {
        // Probar si solo tiene el title_id
        std::string fallback_folder = source_device.root_dir + "/" + game.title_id;
        if (access(fallback_folder.c_str(), F_OK) == 0)
            backup_game_folder = fallback_folder;
    }

    if (access(backup_game_folder.c_str(), F_OK) != 0)
    {
        res.success = false;
        res.message = is_es ? ("No se encontro respaldo previo en " + source_device.label)
                            : ("No previous backup found on " + source_device.label);
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

    std::uint64_t total_bytes = calculate_directory_size(backup_game_folder);

    int files_count = 0;
    std::uint64_t bytes_copied = 0;
    const bool ok = copy_directory_recursive(backup_game_folder, dst_target, files_count,
                                             bytes_copied, total_bytes, progress_cb);

    if (progress_cb)
    {
        OperationProgress p;
        p.bytes_transferred = bytes_copied;
        p.total_bytes = total_bytes > 0 ? total_bytes : bytes_copied;
        p.files_transferred = files_count;
        p.progress = 0.99f;
        p.current_file = is_es ? "Sincronizando vnodes de disco..." : "Syncing vnode caches...";
        progress_cb(p);
    }

    // Sincronizar buffers de disco y vnodes
    sync();

    if (progress_cb)
    {
        OperationProgress p;
        p.bytes_transferred = bytes_copied;
        p.total_bytes = total_bytes > 0 ? total_bytes : bytes_copied;
        p.files_transferred = files_count;
        p.progress = 1.0f;
        p.current_file = is_es ? "Completado" : "Completed";
        progress_cb(p);
    }

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

std::vector<BackupItem> find_available_backups(const scanner::ScannedGame &game,
                                              const std::vector<storage::StorageDevice> &devices)
{
    std::vector<BackupItem> backups;
    const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);

    struct TargetDevice
    {
        std::string id;
        std::string label;
        storage::StorageType type;
        std::string root_dir;
    };

    std::vector<TargetDevice> targets;
    auto add_target = [&](const std::string &id, const std::string &label,
                          storage::StorageType type, const std::string &root_dir) {
        if (root_dir.empty())
            return;
        for (const auto &t : targets)
        {
            if (t.root_dir == root_dir)
                return;
        }
        if (access(root_dir.c_str(), F_OK) == 0)
        {
            targets.push_back({id, label, type, root_dir});
        }
    };

    // 1. Agregar dispositivos provistos (verificando directamente su ruta física sin depender de flags stale)
    for (const auto &dev : devices)
    {
        if (dev.type == storage::StorageType::Cloud)
            continue;
        add_target(dev.id, dev.label, dev.type, dev.root_dir);
    }

    // 2. Verificar directamente la existencia física de las rutas estándar
    // Memoria Interna (/data/MacheteSVG)
    add_target("internal", machete::i18n::tr(machete::i18n::StringId::StorageInternal),
               storage::StorageType::Internal, "/data/" + std::string(storage::ROOT_DIR_NAME));

    // SSD M.2 (/mnt/ext1/MacheteSVG y /mnt/ext0/MacheteSVG)
    add_target("ssd_ext1", machete::i18n::tr(machete::i18n::StringId::StorageM2),
               storage::StorageType::SSD, "/mnt/ext1/" + std::string(storage::ROOT_DIR_NAME));
    add_target("ssd_ext0", machete::i18n::tr(machete::i18n::StringId::StorageM2),
               storage::StorageType::SSD, "/mnt/ext0/" + std::string(storage::ROOT_DIR_NAME));

    // Puertos USB (/mnt/usb0/MacheteSVG .. /mnt/usb7/MacheteSVG)
    for (int i = 0; i < 8; ++i)
    {
        char usb_path[32];
        std::snprintf(usb_path, sizeof(usb_path), "/mnt/usb%d", i);
        char usb_root[64];
        std::snprintf(usb_root, sizeof(usb_root), "%s/%s", usb_path, storage::ROOT_DIR_NAME);
        char label_buf[64];
        std::snprintf(label_buf, sizeof(label_buf),
                      machete::i18n::tr(machete::i18n::StringId::StorageUsbFormat), i, usb_path);
        add_target("usb" + std::to_string(i), label_buf, storage::StorageType::USB, usb_root);
    }

    for (const auto &dev : targets)
    {
        DIR *dir = opendir(dev.root_dir.c_str());
        if (!dir)
            continue;

        while (const dirent *entry = readdir(dir))
        {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
                continue;

            std::string entry_name = entry->d_name;
            if (entry_name.rfind(game.title_id, 0) != 0)
                continue;

            std::string full_path = dev.root_dir + "/" + entry_name;
            struct stat st{};
            if (stat(full_path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
                continue;

            BackupItem item;
            item.device_id = dev.id;
            item.device_label = dev.label;
            item.storage_type = dev.type;
            item.folder_path = full_path;
            item.folder_name = entry_name;
            item.title_id = game.title_id;
            item.title = game.title;

            std::tm *tm_info = std::localtime(&st.st_mtime);
            if (tm_info)
            {
                char time_buf[64];
                std::strftime(time_buf, sizeof(time_buf), "%d/%m/%Y, %I:%M %p", tm_info);
                item.timestamp_str = time_buf;
            }
            else
            {
                item.timestamp_str = is_es ? "Fecha desconocida" : "Unknown date";
            }

            int files = 0;
            std::uint64_t total_size = 0;
            bool keystone_found = false;

            DIR *sub = opendir(full_path.c_str());
            if (sub)
            {
                while (const dirent *se = readdir(sub))
                {
                    if (std::strcmp(se->d_name, ".") == 0 || std::strcmp(se->d_name, "..") == 0)
                        continue;
                    std::string sp = full_path + "/" + se->d_name;
                    struct stat sst{};
                    if (stat(sp.c_str(), &sst) == 0)
                    {
                        if (S_ISREG(sst.st_mode))
                        {
                            files++;
                            total_size += static_cast<std::uint64_t>(sst.st_size);
                        }
                        else if (S_ISDIR(sst.st_mode))
                        {
                            DIR *sub2 = opendir(sp.c_str());
                            if (sub2)
                            {
                                while (const dirent *se2 = readdir(sub2))
                                {
                                    if (std::strcmp(se2->d_name, ".") == 0 || std::strcmp(se2->d_name, "..") == 0)
                                        continue;
                                    std::string sp2 = sp + "/" + se2->d_name;
                                    struct stat sst2{};
                                    if (stat(sp2.c_str(), &sst2) == 0 && S_ISREG(sst2.st_mode))
                                    {
                                        files++;
                                        total_size += static_cast<std::uint64_t>(sst2.st_size);
                                        if (std::strcmp(se2->d_name, "keystone") == 0)
                                            keystone_found = true;
                                    }
                                }
                                closedir(sub2);
                            }
                        }
                    }
                }
                closedir(sub);
            }

            item.file_count = files;
            item.total_bytes = total_size;
            item.has_keystone = keystone_found;

            const double mb = static_cast<double>(total_size) / (1024.0 * 1024.0);
            char size_buf[32];
            if (mb >= 1024.0)
                std::snprintf(size_buf, sizeof(size_buf), "%.1f GB", mb / 1024.0);
            else if (mb >= 0.1)
                std::snprintf(size_buf, sizeof(size_buf), "%.1f MB", mb);
            else
                std::snprintf(size_buf, sizeof(size_buf), "%llu bytes", static_cast<unsigned long long>(total_size));
            item.size_str = size_buf;

            backups.push_back(item);
        }
        closedir(dir);
    }

    return backups;
}

OperationResult restore_backup_item(const scanner::ScannedGame &game,
                                    const BackupItem &backup,
                                    ProgressCallback progress_cb)
{
    OperationResult res;
    const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);

    if (access(backup.folder_path.c_str(), F_OK) != 0)
    {
        res.success = false;
        res.message = is_es ? ("No se encontro la carpeta de respaldo: " + backup.folder_path)
                            : ("Backup folder not found: " + backup.folder_path);
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

    std::uint64_t total_bytes = backup.total_bytes;
    if (total_bytes == 0)
        total_bytes = calculate_directory_size(backup.folder_path);

    int files_count = 0;
    std::uint64_t bytes_copied = 0;
    const bool ok = copy_directory_recursive(backup.folder_path, dst_target, files_count,
                                             bytes_copied, total_bytes, progress_cb);

    if (progress_cb)
    {
        OperationProgress p;
        p.bytes_transferred = bytes_copied;
        p.total_bytes = total_bytes > 0 ? total_bytes : bytes_copied;
        p.files_transferred = files_count;
        p.progress = 0.99f;
        p.current_file = is_es ? "Sincronizando vnodes de disco..." : "Syncing vnode caches...";
        progress_cb(p);
    }

    // Sincronizar buffers de disco y vnodes
    sync();

    if (progress_cb)
    {
        OperationProgress p;
        p.bytes_transferred = bytes_copied;
        p.total_bytes = total_bytes > 0 ? total_bytes : bytes_copied;
        p.files_transferred = files_count;
        p.progress = 1.0f;
        p.current_file = is_es ? "Completado" : "Completed";
        progress_cb(p);
    }

    res.success = ok;
    res.files_transferred = files_count;
    res.bytes_transferred = bytes_copied;

    if (ok)
    {
        res.message = is_es
                          ? ("Restauracion exitosa (" + std::to_string(files_count) +
                             " archivos, " + backup.size_str + ") desde " + backup.device_label)
                          : ("Restore successful (" + std::to_string(files_count) +
                             " files, " + backup.size_str + ") from " + backup.device_label);
    }
    else
    {
        res.message = is_es ? ("Fallo al restaurar archivos en " + game.save_path)
                            : ("Failed to restore files at " + game.save_path);
    }

    return res;
}

} // namespace machete::save
