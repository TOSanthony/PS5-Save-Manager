// MacheteSVG - Sandbox Escape & Jailbreak Daemon Integration Implementation
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/jailbreak.hpp"
#include "core/i18n.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace machete::jailbreak
{

namespace
{

constexpr const char *kEtahenJailbreakPath = "/download0/etahen_jailbreak";
constexpr const char *kElevateProcPath = "/download0/elevate_proc";
constexpr const char *kDataTestPath = "/data";

bool verify_root_filesystem()
{
    // Under ShadowMountPlus sandbox, files in /data may open but opendir("/data") fails with EPERM.
    // Full root elevation allows opendir on /data and /user/home.
    DIR *dir = opendir(kDataTestPath);
    if (dir)
    {
        closedir(dir);
        return true;
    }
    DIR *uhome = opendir("/user/home");
    if (uhome)
    {
        closedir(uhome);
        return true;
    }
    return false;
}

#if defined(__Prospero__) || defined(__PS5__)
struct NetSockaddrIn
{
    std::uint8_t length;
    std::uint8_t family;
    std::uint16_t port;
    std::uint32_t address;
    std::uint16_t virtual_port;
    std::uint8_t zero[6];
};

extern "C"
{
    int sceNetConnect(int socket, const void *address, std::uint32_t address_length);
    int sceNetSend(int socket, const void *data, std::size_t length, int flags);
    int sceNetSetsockopt(int socket, int level, int option, const void *value, std::uint32_t size);
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetSocketClose(int socket);
}
#endif

} // namespace

bool auto_launch_embedded_daemon()
{
    // Search for embedded daemon across standard mount points
    // (matches ProsperoStore app_folder resolution pattern for ShadowMountPlus)
    const char *daemon_paths[] = {
        "/app0/daemon/lapy_jb_daemon.elf",
        "/app0/lapy_jb_daemon.elf",
        "/system_ex/app/PPSA99001/daemon/lapy_jb_daemon.elf",
        "/system_ex/app/PPSA99001/lapy_jb_daemon.elf",
        "/mnt/sandbox/PPSA99001_000/app0/daemon/lapy_jb_daemon.elf",
        "/mnt/sandbox/PPSA99001_000/app0/lapy_jb_daemon.elf",
        "/system_ex/app/PPSA00001/daemon/lapy_jb_daemon.elf",
        "/system_ex/app/PPSA00001/lapy_jb_daemon.elf",
        "/mnt/sandbox/PPSA00001_000/app0/daemon/lapy_jb_daemon.elf",
        "/mnt/sandbox/PPSA00001_000/app0/lapy_jb_daemon.elf",
        "daemon/lapy_jb_daemon.elf",
        "/data/homebrew/lapy_jb/lapy_jb_daemon.elf",
        "payloads/lapy_jb/lapy_jb_daemon.elf",
    };

    std::string found_path;
    for (const char *path : daemon_paths)
    {
        if (access(path, R_OK) == 0)
        {
            found_path = path;
            break;
        }
    }

    if (found_path.empty())
        return false;

    std::FILE *fp = std::fopen(found_path.c_str(), "rb");
    if (!fp)
        return false;

#if defined(__Prospero__) || defined(__PS5__)
    const int socket = sceNetSocket("machete_elfldr", 2, 1, 6);
    if (socket < 0)
    {
        std::fclose(fp);
        return false;
    }

    constexpr int socket_level = 0xffff;
    constexpr int timeout_us = 5'000'000;
    sceNetSetsockopt(socket, socket_level, 0x1105, &timeout_us, sizeof(timeout_us)); // SO_RCVTIMEO
    sceNetSetsockopt(socket, socket_level, 0x1106, &timeout_us, sizeof(timeout_us)); // SO_SNDTIMEO
    sceNetSetsockopt(socket, socket_level, 0x1109, &timeout_us, sizeof(timeout_us)); // SO_CONNECTTIMEO

    constexpr std::uint16_t port = 9021;
    const NetSockaddrIn endpoint{sizeof(NetSockaddrIn),
                                 2,
                                 static_cast<std::uint16_t>((port << 8) | (port >> 8)),
                                 0x0100007f, // 127.0.0.1
                                 0,
                                 {0}};

    if (sceNetConnect(socket, &endpoint, sizeof(endpoint)) < 0)
    {
        sceNetSocketClose(socket);
        std::fclose(fp);
        return false;
    }

    char buffer[16384];
    while (!std::feof(fp))
    {
        const std::size_t count = std::fread(buffer, 1, sizeof(buffer), fp);
        if (count == 0)
            break;

        std::size_t sent_total = 0;
        while (sent_total < count)
        {
            const int sent = sceNetSend(socket, buffer + sent_total, count - sent_total, 0);
            if (sent <= 0)
            {
                sceNetSocketClose(socket);
                std::fclose(fp);
                return false;
            }
            sent_total += static_cast<std::size_t>(sent);
        }
    }

    sceNetSocketClose(socket);
    std::fclose(fp);
    return true;
#else
    std::fclose(fp);
    return true;
#endif
}

JailbreakInfo check_status()
{
    JailbreakInfo info;
    info.pid = static_cast<int>(getpid());
    info.root_access = (getuid() == 0) || verify_root_filesystem();

    const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);
    if (info.root_access)
    {
        info.status = JailbreakStatus::Escaped;
        info.daemon_type = "Lapy JB Daemon / OnionHEN / etaHEN";
        info.status_message =
            is_es ? "Privilegios Root / Sandbox Liberado" : "Root Privileges / Sandbox Escaped";
    }
    else
    {
        info.status = JailbreakStatus::Sandboxed;
        info.daemon_type = is_es ? "Ninguno detectado" : "None detected";
        info.status_message =
            is_es ? "Sandbox Restringido (Requiere Daemon)" : "Restricted Sandbox (Daemon Required)";
    }

    return info;
}

JailbreakInfo request_jailbreak()
{
    JailbreakInfo info = check_status();
    if (info.status == JailbreakStatus::Escaped)
        return info;

    const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);
    const int pid = static_cast<int>(getpid());
    info.pid = pid;

    // 1. Escribir archivos de solicitud de jailbreak:
    // - /download0/etahen_jailbreak (ArkSama Lapy JB Daemon, OnionHEN, etaHEN)
    // - /download0/elevate_proc (mpereiraesaa daemon de ProsperoStore)
    char payload[64];
    std::snprintf(payload, sizeof(payload), "{\"PID\": %d}\n", pid);

    for (const char *path : {kEtahenJailbreakPath, kElevateProcPath})
    {
        std::FILE *fp = std::fopen(path, "w");
        if (fp)
        {
            std::fwrite(payload, 1, std::strlen(payload), fp);
            std::fflush(fp);
            std::fclose(fp);
            info.status = JailbreakStatus::Requested;
        }
    }

    // 2. Comprobar si un daemon ya residente atiende la petición (250ms)
    for (int i = 0; i < 5; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (verify_root_filesystem())
        {
            info.status = JailbreakStatus::Escaped;
            info.root_access = true;
            info.daemon_type = "Lapy JB Daemon / OnionHEN / etaHEN";
            info.status_message =
                is_es ? "Jailbreak Concedido Exitosamente" : "Jailbreak Granted Successfully";
            (void)unlink(kEtahenJailbreakPath);
            (void)unlink(kElevateProcPath);
            return info;
        }
    }

    // 3. Si no hay daemon activo, LANZARLO AUTÓNOMAMENTE mediante ELF loader (127.0.0.1:9021)
    // Idéntico a la arquitectura de ProsperoStore / Homebrew Store
    const bool launched = auto_launch_embedded_daemon();
    if (launched)
    {
        info.daemon_type =
            is_es ? "Lapy JB Daemon (Auto-lanzado)" : "Lapy JB Daemon (Auto-launched)";
    }

    // 4. Esperar a que el daemon (auto-lanzado o residente) complete la elevación (hasta 1.5s)
    for (int i = 0; i < 30; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        const bool file_gone = (access(kEtahenJailbreakPath, F_OK) != 0) || (access(kElevateProcPath, F_OK) != 0);
        const bool root_ok = verify_root_filesystem();

        if (root_ok || (file_gone && i > 1))
        {
            info.status = JailbreakStatus::Escaped;
            info.root_access = true;
            if (info.daemon_type.empty())
                info.daemon_type = "Lapy JB Daemon / OnionHEN";
            info.status_message =
                is_es ? "Jailbreak Concedido Exitosamente" : "Jailbreak Granted Successfully";
            (void)unlink(kEtahenJailbreakPath);
            (void)unlink(kElevateProcPath);
            return info;
        }
    }

    // 5. Verificación final tras el tiempo límite
    (void)unlink(kEtahenJailbreakPath);
    (void)unlink(kElevateProcPath);
    if (verify_root_filesystem())
    {
        info.status = JailbreakStatus::Escaped;
        info.root_access = true;
        info.status_message =
            is_es ? "Privilegios Root Confirmados" : "Root Privileges Confirmed";
    }
    else
    {
        info.status = JailbreakStatus::Sandboxed;
        info.root_access = false;
        info.daemon_type = is_es ? "Inactivo" : "Inactive";
        info.status_message = is_es ? "Sin respuesta del Daemon (Sandbox Activo)"
                                    : "No Daemon Response (Sandbox Active)";
    }

    return info;
}

const char *status_to_string(JailbreakStatus status)
{
    const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);
    switch (status)
    {
    case JailbreakStatus::Escaped:
        return is_es ? "Jailbreak Activo" : "Jailbreak Active";
    case JailbreakStatus::Requested:
        return is_es ? "Solicitando Jailbreak..." : "Requesting Jailbreak...";
    case JailbreakStatus::Sandboxed:
        return is_es ? "Sandbox Restringido" : "Restricted Sandbox";
    default:
        return is_es ? "Desconocido" : "Unknown";
    }
}

} // namespace machete::jailbreak
