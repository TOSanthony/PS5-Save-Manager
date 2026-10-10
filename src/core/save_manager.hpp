// MacheteSVG - Savegame Backup & Restore Operations
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/game_scanner.hpp"
#include "core/storage.hpp"

#include <string>

namespace machete::save
{

struct OperationResult
{
    bool success = false;
    std::string message;
    std::uint64_t bytes_transferred = 0;
    int files_transferred = 0;
};

struct BackupItem
{
    std::string device_id;
    std::string device_label;
    storage::StorageType storage_type;
    std::string folder_path;
    std::string folder_name;
    std::string title_id;
    std::string title;
    std::string timestamp_str;
    std::string size_str;
    int file_count = 0;
    std::uint64_t total_bytes = 0;
    bool has_keystone = false;
};

// Respalda la partida de un juego hacia el dispositivo de almacenamiento seleccionado
// Preserva intacto el archivo keystone y sincroniza las caches vnode (fsync/sync) para evitar CE-108255-1
OperationResult backup_game_save(const scanner::ScannedGame &game,
                                 const storage::StorageDevice &dest_device);

// Restaura una copia de seguridad hacia la ubicacion activa del juego en /user/home/.../savedata/
OperationResult restore_game_save(const scanner::ScannedGame &game,
                                  const storage::StorageDevice &source_device);

// Busca todas las copias de seguridad existentes de un juego en los dispositivos conectados
std::vector<BackupItem> find_available_backups(const scanner::ScannedGame &game,
                                              const std::vector<storage::StorageDevice> &devices);

// Restaura una copia de seguridad especifica elegida por el usuario
OperationResult restore_backup_item(const scanner::ScannedGame &game,
                                    const BackupItem &backup);

} // namespace machete::save
