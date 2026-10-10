// MacheteSVG - Storage Management & USB Hotplug Implementation
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "storage.hpp"
#include "core/i18n.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace machete::storage
{

namespace
{

std::string sanitize_folder_name(std::string name)
{
    const std::string invalid_chars = "\\/:*?\"<>|";
    for (char &c : name)
    {
        if (invalid_chars.find(c) != std::string::npos)
            c = '_';
    }
    return name;
}

std::uint64_t previous_usb_mask = 0;

} // namespace

std::string StorageDevice::free_space_formatted() const
{
    if (type == StorageType::Cloud)
        return "Espacio en Nube";

    if (!connected)
        return "No conectado";

    if (total_bytes == 0)
        return "Listo para respaldar";

    const double gb = static_cast<double>(free_bytes) / (1024.0 * 1024.0 * 1024.0);
    char buf[64];
    if (gb >= 1.0)
        std::snprintf(buf, sizeof(buf), "%.1f GB libres", gb);
    else
    {
        const double mb = static_cast<double>(free_bytes) / (1024.0 * 1024.0);
        std::snprintf(buf, sizeof(buf), "%.0f MB libres", mb);
    }
    return std::string(buf);
}

std::string get_base_path(StorageType type)
{
    switch (type)
    {
    case StorageType::Internal:
        return std::string("/data/") + ROOT_DIR_NAME;
    case StorageType::SSD:
        return std::string("/mnt/ext1/") + ROOT_DIR_NAME;
    case StorageType::USB:
        return std::string("/mnt/usb0/") + ROOT_DIR_NAME;
    case StorageType::Cloud:
        return "Cloud:/" + std::string(ROOT_DIR_NAME);
    default:
        return "";
    }
}

bool is_storage_available(StorageType type)
{
    if (type == StorageType::Cloud)
        return true;

    std::string parent_dir;
    if (type == StorageType::Internal)
        parent_dir = "/data";
    else if (type == StorageType::SSD)
        parent_dir = "/mnt/ext1";
    else if (type == StorageType::USB)
        parent_dir = "/mnt/usb0";

    DIR *dir = opendir(parent_dir.c_str());
    if (dir)
    {
        closedir(dir);
        return true;
    }
    return false;
}

std::vector<StorageDevice> scan_storage_devices()
{
    std::vector<StorageDevice> devices;

    // 1. Memoria Interna (/data)
    {
        StorageDevice dev;
        dev.type = StorageType::Internal;
        dev.id = "internal";
        dev.label = machete::i18n::tr(machete::i18n::StringId::StorageInternal);
        dev.mount_path = "/data";
        dev.root_dir = "/data/" + std::string(ROOT_DIR_NAME);

        DIR *dir = opendir("/data");
        if (dir)
        {
            closedir(dir);
            dev.connected = true;
        }
        else if (access("/data", F_OK) == 0)
        {
            dev.connected = true;
        }

        if (dev.connected)
        {
            struct statvfs vfs;
            if (statvfs("/data", &vfs) == 0)
            {
                dev.free_bytes = static_cast<std::uint64_t>(vfs.f_bavail) * vfs.f_frsize;
                dev.total_bytes = static_cast<std::uint64_t>(vfs.f_blocks) * vfs.f_frsize;
            }
        }
        devices.push_back(dev);
    }

    // 2. SSD de Expansión M.2 (/mnt/ext1 o /mnt/ext0)
    {
        StorageDevice dev;
        dev.type = StorageType::SSD;
        dev.id = "ssd_ext1";
        dev.label = machete::i18n::tr(machete::i18n::StringId::StorageM2);
        dev.mount_path = "/mnt/ext1";
        dev.root_dir = "/mnt/ext1/" + std::string(ROOT_DIR_NAME);

        DIR *dir = opendir("/mnt/ext1");
        if (!dir)
        {
            dir = opendir("/mnt/ext0");
            if (dir)
            {
                dev.mount_path = "/mnt/ext0";
                dev.root_dir = "/mnt/ext0/" + std::string(ROOT_DIR_NAME);
            }
        }

        if (dir)
        {
            closedir(dir);
            dev.connected = true;
            struct statvfs vfs;
            if (statvfs(dev.mount_path.c_str(), &vfs) == 0)
            {
                dev.free_bytes = static_cast<std::uint64_t>(vfs.f_bavail) * vfs.f_frsize;
                dev.total_bytes = static_cast<std::uint64_t>(vfs.f_blocks) * vfs.f_frsize;
            }
            devices.push_back(dev);
        }
    }

    // 3. Escaneo dinámico de puertos USB (/mnt/usb0 .. /mnt/usb7)
    for (int i = 0; i < 8; ++i)
    {
        char usb_path[32];
        std::snprintf(usb_path, sizeof(usb_path), "/mnt/usb%d", i);

        DIR *dir = opendir(usb_path);
        if (dir)
        {
            closedir(dir);
            StorageDevice dev;
            dev.type = StorageType::USB;
            dev.id = "usb" + std::to_string(i);

            char label_buf[64];
            std::snprintf(label_buf, sizeof(label_buf),
                          machete::i18n::tr(machete::i18n::StringId::StorageUsbFormat), i, usb_path);
            dev.label = label_buf;
            dev.mount_path = usb_path;
            dev.root_dir = std::string(usb_path) + "/" + ROOT_DIR_NAME;
            dev.connected = true;

            struct statvfs vfs;
            if (statvfs(usb_path, &vfs) == 0)
            {
                dev.free_bytes = static_cast<std::uint64_t>(vfs.f_bavail) * vfs.f_frsize;
                dev.total_bytes = static_cast<std::uint64_t>(vfs.f_blocks) * vfs.f_frsize;
            }
            devices.push_back(dev);
        }
    }

    // 4. Google Drive (Nube)
    {
        StorageDevice dev;
        dev.type = StorageType::Cloud;
        dev.id = "cloud_gdrive";
        dev.label = machete::i18n::tr(machete::i18n::StringId::StorageCloud);
        dev.mount_path = "Cloud:";
        dev.root_dir = "Cloud:/" + std::string(ROOT_DIR_NAME);
        dev.connected = true;
        devices.push_back(dev);
    }

    return devices;
}

bool poll_storage_changes(std::vector<StorageDevice> &out_devices, std::string &change_description)
{
    out_devices = scan_storage_devices();

    std::uint64_t current_usb_mask = 0;
    for (const auto &dev : out_devices)
    {
        if (dev.type == StorageType::USB && dev.id.rfind("usb", 0) == 0 && dev.id.length() > 3)
        {
            if (dev.id[3] >= '0' && dev.id[3] <= '7')
            {
                int index = dev.id[3] - '0';
                current_usb_mask |= (1ULL << index);
            }
        }
    }

    // Primera ejecución: solo registrar el estado inicial
    static bool first_run = true;
    if (first_run)
    {
        previous_usb_mask = current_usb_mask;
        first_run = false;
        return false;
    }

    if (current_usb_mask != previous_usb_mask)
    {
        for (int i = 0; i < 8; ++i)
        {
            const bool was_on = (previous_usb_mask & (1ULL << i)) != 0;
            const bool is_on = (current_usb_mask & (1ULL << i)) != 0;
            if (!was_on && is_on)
            {
                char desc_buf[128];
                std::snprintf(desc_buf, sizeof(desc_buf),
                              machete::i18n::tr(machete::i18n::StringId::UsbConnectedFormat), i);
                change_description = desc_buf;
                previous_usb_mask = current_usb_mask;
                return true;
            }
            else if (was_on && !is_on)
            {
                char desc_buf[128];
                std::snprintf(desc_buf, sizeof(desc_buf),
                              machete::i18n::tr(machete::i18n::StringId::UsbDisconnectedFormat), i);
                change_description = desc_buf;
                previous_usb_mask = current_usb_mask;
                return true;
            }
        }
        previous_usb_mask = current_usb_mask;
        return true;
    }

    return false;
}

bool ensure_directory_structure_at(const std::string &mount_path, const std::string &title_id,
                                   const std::string &game_name)
{
    std::string base = mount_path + "/" + ROOT_DIR_NAME;
    mkdir(base.c_str(), 0777);

    if (title_id.empty() && game_name.empty())
        return true;

    std::string safe_name = sanitize_folder_name(game_name);
    std::string folder_name = title_id.empty() ? safe_name : (title_id + " - " + safe_name);
    std::string game_dir = base + "/" + folder_name;
    int res = mkdir(game_dir.c_str(), 0777);

    return (res == 0 || errno == EEXIST);
}

bool ensure_directory_structure(StorageType type, const std::string &title_id,
                                const std::string &game_name)
{
    if (type == StorageType::Cloud)
        return true;

    std::string mount_point;
    switch (type)
    {
    case StorageType::Internal:
        mount_point = "/data";
        break;
    case StorageType::SSD:
        mount_point = "/mnt/ext1";
        break;
    case StorageType::USB:
        mount_point = "/mnt/usb0";
        break;
    default:
        mount_point = "/data";
        break;
    }

    return ensure_directory_structure_at(mount_point, title_id, game_name);
}

} // namespace machete::storage
