// MacheteSVG - Savegame Backup & Restore Operations Implementation
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_manager.hpp"
#include "core/i18n.hpp"
#include "core/jailbreak.hpp"
#include "core/logger.hpp"
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

bool ensure_directories_recursive(const std::string &path)
{
    if (path.empty())
        return false;

    // Si ya existe fisicamente como directorio, no hacer nada para no alterar permisos de rutas del sistema
    struct stat st{};
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        return true;

    std::string current;
    for (std::size_t i = 0; i < path.size(); ++i)
    {
        current += path[i];
        if (path[i] == '/' || i == path.size() - 1)
        {
            if (current.empty() || current == "/")
                continue;

            // Si el componente ya existe fisicamente (como /user, /user/home, /data), omitir
            if (stat(current.c_str(), &st) == 0)
                continue;

            // Solo crear directorios que realmente no existian
            if (mkdir(current.c_str(), 0777) == 0)
            {
                (void)chmod(current.c_str(), 0777);
            }
        }
    }
    return true;
}

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
    {
        MACHETE_LOG("[COPY]", "Fallo al abrir archivo de origen: %s (errno=%d: %s)",
                    src.c_str(), errno, std::strerror(errno));
        return false;
    }

    // Si el archivo de destino ya existe, asegurar permisos de escritura para evitar EACCES (archivos 0444)
    if (access(dst.c_str(), F_OK) == 0)
    {
        (void)chmod(dst.c_str(), 0666);
    }

    std::FILE *out = std::fopen(dst.c_str(), "wb");
    if (!out)
    {
        // En caso de bloqueo del sistema de archivos, forzar desenlace (unlink) y reintentar
        (void)unlink(dst.c_str());
        out = std::fopen(dst.c_str(), "wb");
        if (!out)
        {
            MACHETE_LOG("[COPY]", "Fallo al abrir archivo de destino para escritura: %s (errno=%d: %s)",
                        dst.c_str(), errno, std::strerror(errno));
            std::fclose(in);
            return false;
        }
    }

    std::vector<char> buffer(64 * 1024);
    std::size_t n = 0;
    bool ok = true;
    std::uint64_t last_cb_bytes = bytes_copied;

    while ((n = std::fread(buffer.data(), 1, buffer.size(), in)) > 0)
    {
        if (std::fwrite(buffer.data(), 1, n, out) != n)
        {
            MACHETE_LOG("[COPY]", "Error escribiendo en destino %s (errno=%d: %s)",
                        dst.c_str(), errno, std::strerror(errno));
            ok = false;
            break;
        }
        bytes_copied += n;
        // Notificar progreso cada 256 KB o al finalizar para no saturar el mutex del hilo principal
        if (progress_cb && (bytes_copied - last_cb_bytes >= 256 * 1024 || bytes_copied == total_bytes))
        {
            last_cb_bytes = bytes_copied;
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
    std::fclose(out);
    std::fclose(in);

    // Otorgar permisos abiertos al destino para que Prospero y los juegos puedan leerlo libremente
    (void)chmod(dst.c_str(), 0666);
    MACHETE_LOG("[COPY]", "Archivo completado: %s", dst.c_str());
    ::machete::log::flush();
    return ok;
}

bool copy_directory_recursive(const std::string &src_dir, const std::string &dst_dir,
                              int &files_count, std::uint64_t &bytes_copied,
                              std::uint64_t total_bytes,
                              const ProgressCallback &progress_cb,
                              bool is_restoring = false)
{
    struct stat src_st{};
    if (stat(src_dir.c_str(), &src_st) == 0 && S_ISREG(src_st.st_mode))
    {
        ensure_directories_recursive(dst_dir);
        std::size_t slash = src_dir.find_last_of('/');
        std::string filename = (slash != std::string::npos) ? src_dir.substr(slash + 1) : src_dir;
        std::string dst_file = dst_dir + "/" + filename;

        // Preservación estricta de Keystone: no sobreescribir el keystone local de la consola si existe
        if (is_restoring && (filename == "keystone" || filename == "KEYSTONE"))
        {
            if (access(dst_file.c_str(), F_OK) == 0)
            {
                MACHETE_LOG("[RESTORE]", "Preservando keystone original de la consola: %s", dst_file.c_str());
                files_count++;
                return true;
            }
        }

        if (copy_file_with_fsync(src_dir, dst_file, bytes_copied, total_bytes, files_count, progress_cb))
        {
            files_count++;
            return true;
        }
        return false;
    }

    ensure_directories_recursive(dst_dir);

    DIR *dir = opendir(src_dir.c_str());
    if (!dir)
    {
        MACHETE_LOG("[COPY]", "No se pudo abrir directorio origen: %s (errno=%d: %s)",
                    src_dir.c_str(), errno, std::strerror(errno));
        return false;
    }

    bool all_ok = true;
    while (const dirent *entry = readdir(dir))
    {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;

        const std::string src_path = src_dir + "/" + entry->d_name;
        const std::string dst_path = dst_dir + "/" + entry->d_name;

        // Preservación de Keystone durante la restauración
        if (is_restoring && (std::strcmp(entry->d_name, "keystone") == 0 || std::strcmp(entry->d_name, "KEYSTONE") == 0))
        {
            if (access(dst_path.c_str(), F_OK) == 0)
            {
                MACHETE_LOG("[RESTORE]", "Preservando keystone original de consola: %s", dst_path.c_str());
                files_count++;
                continue;
            }
        }

        struct stat st{};
        if (stat(src_path.c_str(), &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                ensure_directories_recursive(dst_path);
                if (!copy_directory_recursive(src_path, dst_path, files_count, bytes_copied, total_bytes, progress_cb, is_restoring))
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

std::string resolve_default_save_path(const scanner::ScannedGame &game)
{
    DIR *home = opendir("/user/home");
    if (!home)
        return "";

    std::string user_id;
    while (const dirent *ent = readdir(home))
    {
        if (ent->d_name[0] == '.')
            continue;
        user_id = ent->d_name;
        break; // Tomar el primer identificador de usuario activo
    }
    closedir(home);

    if (user_id.empty())
        return "";

    const bool is_ps5 = (game.platform == "PS5" || game.title_id.rfind("PPSA", 0) == 0);
    const std::string base_dir = std::string("/user/home/") + user_id + "/" +
                                 (is_ps5 ? "savedata_prospero" : "savedata");

    const std::string target = base_dir + "/" + game.title_id;
    ensure_directories_recursive(target);
    return target;
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

    if (!jailbreak::verify_root_access(static_cast<int>(getpid())))
    {
        MACHETE_LOG("[BACKUP]", "Privilegios root ausentes (UID=%d, EUID=%d). Solicitando elevacion...",
                    static_cast<int>(getuid()), static_cast<int>(geteuid()));
        const auto jb = jailbreak::request_jailbreak();
        if (!jb.root_access)
        {
            res.success = false;
            res.message = is_es
                ? "Fallo de privilegios: se requiere jailbreak (UID 0) para acceder a los datos de guardado"
                : "Privilege failure: jailbreak (UID 0) required to access save data";
            MACHETE_LOG("[BACKUP]", "Elevacion fallida. Operacion abortada de forma segura.");
            return res;
        }
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
        res.success = false;
        res.message = is_es
            ? ("No se encontraron datos de guardado en consola para " + game.title_id)
            : ("No save data found on console for " + game.title_id);
        MACHETE_LOG("[BACKUP]", "Fallo: save_path '%s' inaccesible para %s",
                    game.save_path.c_str(), game.title_id.c_str());
        return res;
    }

    // Ruta destino: <root_dir>/<title_id> - <title>/
    const std::string dest_game_folder =
        root_dir + "/" + game.title_id + " - " + sanitize_name(game.title);

    ensure_directories_recursive(root_dir);
    ensure_directories_recursive(dest_game_folder);

    std::uint64_t total_bytes = game.total_bytes;
    if (total_bytes == 0)
        total_bytes = calculate_directory_size(game.save_path);

    int files_count = 0;
    std::uint64_t bytes_copied = 0;
    MACHETE_LOG("[BACKUP]", "Iniciando respaldo de %s hacia %s",
                game.save_path.c_str(), dest_game_folder.c_str());
    const bool ok = copy_directory_recursive(game.save_path, dest_game_folder, files_count,
                                             bytes_copied, total_bytes, progress_cb, false);

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

    res.success = ok && (files_count > 0);
    res.files_transferred = files_count;
    res.bytes_transferred = bytes_copied;

    if (res.success)
    {
        res.message = is_es
                          ? ("Respaldo completado (" + std::to_string(files_count) +
                             " archivos) en " + dest_game_folder)
                          : ("Backup completed (" + std::to_string(files_count) +
                             " files) at " + dest_game_folder);
        MACHETE_LOG("[BACKUP]", "Respaldo exitoso: %d archivos, %llu bytes",
                    files_count, static_cast<unsigned long long>(bytes_copied));
    }
    else
    {
        res.message = is_es
                          ? ("Aviso: Copia fallida o 0 archivos copiados. Revisa permisos de unidad.")
                          : ("Warning: Copy failed or 0 files copied. Check storage permissions.");
        MACHETE_LOG("[BACKUP]", "Fallo al respaldar %s hacia %s", game.title_id.c_str(), dest_game_folder.c_str());
    }

    return res;
}

OperationResult restore_game_save(const scanner::ScannedGame &game,
                                  const storage::StorageDevice &source_device,
                                  ProgressCallback progress_cb)
{
    OperationResult res;
    const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);

    if (!jailbreak::verify_root_access(static_cast<int>(getpid())))
    {
        MACHETE_LOG("[RESTORE]", "Privilegios root ausentes (UID=%d, EUID=%d). Solicitando elevacion...",
                    static_cast<int>(getuid()), static_cast<int>(geteuid()));
        const auto jb = jailbreak::request_jailbreak();
        if (!jb.root_access)
        {
            res.success = false;
            res.message = is_es
                ? "Fallo de privilegios: se requiere jailbreak (UID 0) para restaurar en /user/home"
                : "Privilege failure: jailbreak (UID 0) required to restore to /user/home";
            MACHETE_LOG("[RESTORE]", "Elevacion fallida. Operacion abortada de forma segura.");
            return res;
        }
    }

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
        MACHETE_LOG("[RESTORE]", "No se encontro carpeta de respaldo: %s", backup_game_folder.c_str());
        return res;
    }

    const std::uint64_t total_dir_bytes = calculate_directory_size(backup_game_folder);
    if (total_dir_bytes == 0)
    {
        res.success = false;
        res.message = is_es ? "La carpeta de respaldo esta vacia (0 bytes)"
                            : "Backup folder is empty (0 bytes)";
        MACHETE_LOG("[RESTORE]", "Error: respaldo vacio en %s", backup_game_folder.c_str());
        return res;
    }

    std::string dst_target = game.save_path;
    if (dst_target.empty())
    {
        dst_target = resolve_default_save_path(game);
        MACHETE_LOG("[RESTORE]", "Ruta resuelta dinamicamente para %s: %s",
                    game.title_id.c_str(), dst_target.c_str());
    }

    if (dst_target.empty())
    {
        res.success = false;
        res.message = is_es ? "Ruta de guardado del juego no localizada en la consola"
                            : "Game save path not located on console";
        MACHETE_LOG("[RESTORE]", "No se pudo determinar destino para %s", game.title_id.c_str());
        return res;
    }

    struct stat save_st{};
    if (stat(dst_target.c_str(), &save_st) == 0 && S_ISREG(save_st.st_mode))
    {
        std::size_t slash = dst_target.find_last_of('/');
        if (slash != std::string::npos)
            dst_target = dst_target.substr(0, slash);
    }

    ensure_directories_recursive(dst_target);

    int files_count = 0;
    std::uint64_t bytes_copied = 0;
    MACHETE_LOG("[RESTORE]", "Restaurando desde %s hacia %s",
                backup_game_folder.c_str(), dst_target.c_str());
    const bool ok = copy_directory_recursive(backup_game_folder, dst_target, files_count,
                                             bytes_copied, total_dir_bytes, progress_cb, true);

    if (progress_cb)
    {
        OperationProgress p;
        p.bytes_transferred = bytes_copied;
        p.total_bytes = total_dir_bytes > 0 ? total_dir_bytes : bytes_copied;
        p.files_transferred = files_count;
        p.progress = 1.0f;
        p.current_file = is_es ? "Completado" : "Completed";
        progress_cb(p);
    }

    res.success = ok && (files_count > 0);
    res.files_transferred = files_count;
    res.bytes_transferred = bytes_copied;

    if (res.success)
    {
        res.message = is_es
                          ? ("Restauracion exitosa (" + std::to_string(files_count) +
                             " archivos) con Keystone preservado")
                          : ("Restore successful (" + std::to_string(files_count) +
                             " files) with Keystone preserved");
        MACHETE_LOG("[RESTORE]", "Restauracion completada exitosamente: %d archivos", files_count);
    }
    else
    {
        res.message = is_es ? ("Fallo al restaurar archivos en " + dst_target)
                            : ("Failed to restore files at " + dst_target);
        MACHETE_LOG("[RESTORE]", "Error al restaurar archivos en %s", dst_target.c_str());
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

    if (!jailbreak::verify_root_access(static_cast<int>(getpid())))
    {
        MACHETE_LOG("[RESTORE]", "Privilegios root ausentes (UID=%d, EUID=%d). Solicitando elevacion...",
                    static_cast<int>(getuid()), static_cast<int>(geteuid()));
        const auto jb = jailbreak::request_jailbreak();
        if (!jb.root_access)
        {
            res.success = false;
            res.message = is_es
                ? "Fallo de privilegios: se requiere jailbreak (UID 0) para restaurar en /user/home"
                : "Privilege failure: jailbreak (UID 0) required to restore to /user/home";
            MACHETE_LOG("[RESTORE]", "Elevacion fallida. Operacion abortada de forma segura.");
            return res;
        }
    }

    if (access(backup.folder_path.c_str(), F_OK) != 0)
    {
        res.success = false;
        res.message = is_es ? ("No se encontro la carpeta de respaldo: " + backup.folder_path)
                            : ("Backup folder not found: " + backup.folder_path);
        MACHETE_LOG("[RESTORE]", "Carpeta de respaldo no accesible: %s", backup.folder_path.c_str());
        return res;
    }

    const std::uint64_t total_dir_bytes = calculate_directory_size(backup.folder_path);
    if (total_dir_bytes == 0)
    {
        res.success = false;
        res.message = is_es ? "La carpeta de respaldo esta vacia (0 bytes)"
                            : "Backup folder is empty (0 bytes)";
        MACHETE_LOG("[RESTORE]", "Carpeta de respaldo vacia (0 bytes): %s", backup.folder_path.c_str());
        return res;
    }

    std::string dst_target = game.save_path;
    if (dst_target.empty())
    {
        dst_target = resolve_default_save_path(game);
        MACHETE_LOG("[RESTORE]", "Ruta resuelta dinamicamente para %s: %s",
                    game.title_id.c_str(), dst_target.c_str());
    }

    if (dst_target.empty())
    {
        res.success = false;
        res.message = is_es ? "Ruta de guardado del juego no localizada en la consola"
                            : "Game save path not located on console";
        MACHETE_LOG("[RESTORE]", "No se localizo destino de guardado para %s", game.title_id.c_str());
        return res;
    }

    struct stat save_st{};
    if (stat(dst_target.c_str(), &save_st) == 0 && S_ISREG(save_st.st_mode))
    {
        std::size_t slash = dst_target.find_last_of('/');
        if (slash != std::string::npos)
            dst_target = dst_target.substr(0, slash);
    }

    ensure_directories_recursive(dst_target);

    std::uint64_t total_bytes = backup.total_bytes;
    if (total_bytes == 0)
        total_bytes = total_dir_bytes;

    int files_count = 0;
    std::uint64_t bytes_copied = 0;
    MACHETE_LOG("[RESTORE]", "Iniciando copia recursiva desde %s hacia %s (total=%llu bytes)",
                backup.folder_path.c_str(), dst_target.c_str(), static_cast<unsigned long long>(total_bytes));
    const bool ok = copy_directory_recursive(backup.folder_path, dst_target, files_count,
                                             bytes_copied, total_bytes, progress_cb, true);

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

    res.success = ok && (files_count > 0);
    res.files_transferred = files_count;
    res.bytes_transferred = bytes_copied;

    if (res.success)
    {
        res.message = is_es
                          ? ("Restauracion exitosa (" + std::to_string(files_count) +
                             " archivos, " + backup.size_str + ") desde " + backup.device_label)
                          : ("Restore successful (" + std::to_string(files_count) +
                             " files, " + backup.size_str + ") from " + backup.device_label);
        MACHETE_LOG("[RESTORE]", "Restauracion exitosa: %d archivos transferidos", files_count);
    }
    else
    {
        res.message = is_es ? ("Fallo al restaurar archivos en " + dst_target)
                            : ("Failed to restore files at " + dst_target);
        MACHETE_LOG("[RESTORE]", "Fallo al restaurar archivos en %s (archivos copiados: %d)",
                    dst_target.c_str(), files_count);
    }

    return res;
}

} // namespace machete::save
