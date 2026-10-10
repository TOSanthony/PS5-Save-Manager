// MacheteSVG - Google Drive Cloud Sync Module
// OAuth 2.0 Device Authorization Grant (RFC 8628) & Google Drive v3 REST API
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace machete::cloud
{

enum class AuthState
{
    Unauthenticated,
    RequestingCode,
    WaitingForUser, // El usuario introduce el código en google.com/device
    Authorized,
    TokenExpired,
    Error
};

enum class SyncState
{
    Idle,
    CheckingFolders,
    Uploading,
    Downloading,
    Complete,
    Failed
};

struct DeviceCodeInfo
{
    std::string device_code;
    std::string user_code;        // ej. "WDJB-HGTR"
    std::string verification_url; // "https://www.google.com/device"
    int expires_in = 1800;
    int interval = 5;
};

struct CloudFile
{
    std::string id;
    std::string name;
    std::string mime_type;
    std::size_t size = 0;
    std::string modified_time;
};

class GoogleDrive
{
  public:
    static GoogleDrive &instance();

    // Gestión de Autenticación
    void start_device_auth(std::function<void(const DeviceCodeInfo &)> on_code_received);
    void cancel_auth();
    bool is_authorized() const;
    void logout();

    AuthState auth_state() const
    {
        return auth_state_.load();
    }
    SyncState sync_state() const
    {
        return sync_state_.load();
    }
    float sync_progress() const
    {
        return sync_progress_.load();
    }
    std::string last_error() const;
    std::string user_code() const;

    // Operaciones en la nube
    // Respalda la partida de un juego en la estructura /MacheteSVG/[TitleID - Nombre]/
    void backup_save(const std::string &title_id, const std::string &game_name,
                     const std::string &local_save_path,
                     std::function<void(bool success, const std::string &msg)> on_complete);

    // Lista las partidas existentes para un juego en Google Drive
    void list_saves(const std::string &title_id, const std::string &game_name,
                    std::function<void(const std::vector<CloudFile> &files)> on_complete);

    // Descarga y restaura una partida desde Google Drive
    void restore_save(const std::string &cloud_file_id, const std::string &dest_local_path,
                      std::function<void(bool success, const std::string &msg)> on_complete);

  private:
    GoogleDrive();
    ~GoogleDrive();

    bool load_tokens();
    bool save_tokens();
    bool refresh_access_token();

    std::atomic<AuthState> auth_state_{AuthState::Unauthenticated};
    std::atomic<SyncState> sync_state_{SyncState::Idle};
    std::atomic<float> sync_progress_{0.0f};

    std::string access_token_;
    std::string refresh_token_;
    std::string user_code_;
    std::string root_folder_id_; // ID de la carpeta MacheteSVG
    std::string last_error_;

    mutable std::mutex mutex_;
};

} // namespace machete::cloud
