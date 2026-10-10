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

// Respalda la partida de un juego hacia el dispositivo de almacenamiento seleccionado
// Preserva intacto el archivo keystone y sincroniza las cachés vnode (fsync/sync) para evitar CE-108255-1
OperationResult backup_game_save(const scanner::ScannedGame &game,
                                 const storage::StorageDevice &dest_device);

// Restaura una copia de seguridad hacia la ubicación activa del juego en /user/home/.../savedata/
OperationResult restore_game_save(const scanner::ScannedGame &game,
                                  const storage::StorageDevice &source_device);

} // namespace machete::save
