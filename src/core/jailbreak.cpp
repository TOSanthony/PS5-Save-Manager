// MacheteSVG - Sandbox Escape & Jailbreak Daemon Integration Implementation
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/jailbreak.hpp"
#include "core/embedded_daemon.hpp"
#include "core/i18n.hpp"

#include <array>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace machete::jailbreak
{

namespace
{

constexpr const char *kEtahenJailbreakPath = "/download0/etahen_jailbreak";
constexpr const char *kElevateProcPath = "/download0/elevate_proc";
constexpr const char *kLapyResultPath = "/download0/lapy_owned_result";

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
    int sceNetRecv(int socket, void *data, std::size_t length, int flags);
    int sceNetSetsockopt(int socket, int level, int option, const void *value, std::uint32_t size);
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetSocketClose(int socket);
}
#endif

bool write_all_fd(int fd, const void *data, std::size_t length) noexcept
{
    const auto *bytes = static_cast<const char *>(data);
    while (length != 0)
    {
        const auto count = write(fd, bytes, length);
        if (count <= 0 || static_cast<std::size_t>(count) > length)
            return false;
        bytes += count;
        length -= static_cast<std::size_t>(count);
    }
    return true;
}

// Verifica el escape del sandbox y privilegios de lectura/escritura en /data y /user
bool verify_root_access(int pid) noexcept
{
    // 1. Si el UID ya es 0 (root efectivo), privilegios concedidos
    if (getuid() == 0 || geteuid() == 0)
        return true;

    // 2. Prueba definitiva de lectura/escritura bajo /data (patrón de verificación ProsperoStore)
    char probe_path[96];
    std::snprintf(probe_path, sizeof(probe_path), "/data/.machete_probe_%d", pid);
    (void)unlink(probe_path);

    const int fd = open(probe_path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0)
    {
        constexpr const char kProbeToken[] = "MACHETE_ROOT_OK\n";
        constexpr std::size_t kTokenLen = sizeof(kProbeToken) - 1;
        char read_buf[32]{};

        bool ok = write_all_fd(fd, kProbeToken, kTokenLen);
        if (ok && lseek(fd, 0, SEEK_SET) == 0)
        {
            const auto bytes_read = read(fd, read_buf, kTokenLen);
            ok = (bytes_read == static_cast<ssize_t>(kTokenLen)) &&
                 (std::memcmp(read_buf, kProbeToken, kTokenLen) == 0);
        }
        close(fd);
        (void)unlink(probe_path);
        if (ok)
            return true;
    }

    // 3. Prueba de listado de directorios privilegiados de PlayStation 5
    DIR *dir_home = opendir("/user/home");
    if (dir_home)
    {
        closedir(dir_home);
        return true;
    }

    DIR *dir_app = opendir("/user/app");
    if (dir_app)
    {
        closedir(dir_app);
        return true;
    }

    return false;
}

// Publica la petición de elevación atómicamente mediante archivo temporal + rename
bool publish_atomic_request(const char *target_path, int pid) noexcept
{
    char tmp_path[128];
    char body[64];
    const int body_len = std::snprintf(body, sizeof(body), "{\"PID\": %d}\n", pid);
    const int tmp_len = std::snprintf(tmp_path, sizeof(tmp_path), "%s.%d.tmp", target_path, pid);

    if (body_len <= 0 || tmp_len <= 0)
        return false;

    (void)unlink(tmp_path);
    const int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;

    if (!write_all_fd(fd, body, static_cast<std::size_t>(body_len)))
    {
        close(fd);
        (void)unlink(tmp_path);
        return false;
    }

    close(fd);
    (void)chmod(tmp_path, 0666);

    if (rename(tmp_path, target_path) != 0)
    {
        (void)unlink(tmp_path);
        return false;
    }

    return true;
}

} // namespace

bool auto_launch_embedded_daemon()
{
#if defined(__Prospero__) || defined(__PS5__)
    // Obtener los datos del demonio: probar rutas locales o usar el binario embebido en memoria
    const char *daemon_paths[] = {
        "/app0/lapy.elf",
        "/app0/daemon/lapy_jb_daemon.elf",
        "/system_ex/app/PPSA99001/lapy.elf",
        "/system_ex/app/PPSA99001/daemon/lapy_jb_daemon.elf",
        "/data/homebrew/PPSA99001/lapy.elf",
        "/data/homebrew/PPSA99001/daemon/lapy_jb_daemon.elf",
        "lapy.elf",
        "daemon/lapy_jb_daemon.elf",
    };

    const void *payload_data = nullptr;
    std::size_t payload_size = 0;
    std::vector<std::uint8_t> disk_buffer;

    for (const char *path : daemon_paths)
    {
        const int fd = open(path, O_RDONLY);
        if (fd >= 0)
        {
            struct stat st{};
            if (fstat(fd, &st) == 0 && st.st_size > 1024)
            {
                disk_buffer.resize(static_cast<std::size_t>(st.st_size));
                auto bytes = read(fd, disk_buffer.data(), disk_buffer.size());
                if (bytes == st.st_size)
                {
                    payload_data = disk_buffer.data();
                    payload_size = disk_buffer.size();
                    close(fd);
                    break;
                }
            }
            close(fd);
        }
    }

    // Si no está en disco o el sandbox impide leerlo, utilizar la copia embebida en memoria
    if (!payload_data || payload_size == 0)
    {
        payload_data = kEmbeddedDaemonElf;
        payload_size = kEmbeddedDaemonElfSize;
    }

    if (!payload_data || payload_size == 0)
        return false;

    // Conectar al cargador ELF local (probar puerto primario 9021 y alternativo 9020)
    for (const std::uint16_t port : {static_cast<std::uint16_t>(9021), static_cast<std::uint16_t>(9020)})
    {
        const int socket = sceNetSocket("machete_elfldr", 2, 1, 6);
        if (socket < 0)
            continue;

        constexpr int socket_level = 0xffff;
        constexpr int timeout_us = 4'000'000;
        sceNetSetsockopt(socket, socket_level, 0x1105, &timeout_us, sizeof(timeout_us)); // SO_RCVTIMEO
        sceNetSetsockopt(socket, socket_level, 0x1106, &timeout_us, sizeof(timeout_us)); // SO_SNDTIMEO
        sceNetSetsockopt(socket, socket_level, 0x1109, &timeout_us, sizeof(timeout_us)); // SO_CONNECTTIMEO

        const NetSockaddrIn endpoint{sizeof(NetSockaddrIn),
                                     2,
                                     static_cast<std::uint16_t>((port << 8) | (port >> 8)),
                                     0x0100007f, // 127.0.0.1
                                     0,
                                     {0}};

        if (sceNetConnect(socket, &endpoint, sizeof(endpoint)) < 0)
        {
            sceNetSocketClose(socket);
            continue;
        }

        // Transmitir todo el binario ELF al cargador
        const auto *bytes = static_cast<const char *>(payload_data);
        std::size_t remaining = payload_size;
        bool stream_ok = true;

        while (remaining > 0)
        {
            const std::size_t chunk = std::min<std::size_t>(remaining, 16384);
            const int sent = sceNetSend(socket, bytes, chunk, 0);
            if (sent <= 0)
            {
                stream_ok = false;
                break;
            }
            bytes += sent;
            remaining -= static_cast<std::size_t>(sent);
        }

        // Cerrar socket para señalizar EOF al cargador ELF y permitir su ejecución
        sceNetSocketClose(socket);

        if (stream_ok)
            return true;
    }

    return false;
#else
    return true;
#endif
}

JailbreakInfo check_status()
{
    JailbreakInfo info;
    info.pid = static_cast<int>(getpid());
    info.root_access = verify_root_access(info.pid);

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
    const int pid = static_cast<int>(getpid());
    const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);

    // 1. Verificación preliminar rápida: si ya tenemos acceso root, retornar de inmediato
    if (verify_root_access(pid))
    {
        JailbreakInfo info;
        info.pid = pid;
        info.status = JailbreakStatus::Escaped;
        info.root_access = true;
        info.daemon_type = "Lapy JB Daemon / OnionHEN / etaHEN";
        info.status_message =
            is_es ? "Privilegios Root Confirmados" : "Root Privileges Confirmed";
        return info;
    }

    JailbreakInfo info;
    info.pid = pid;

    // 2. Publicar solicitudes atómicas para demonios residentes:
    // - /download0/etahen_jailbreak (ArkSama Lapy JB Daemon, OnionHEN, etaHEN)
    // - /download0/elevate_proc (ProsperoStore / Lapy-JB-Daemon de mpereiraesaa)
    publish_atomic_request(kEtahenJailbreakPath, pid);
    publish_atomic_request(kElevateProcPath, pid);
    info.status = JailbreakStatus::Requested;

    // 3. Sondeo para demonio residente activo (hasta 1.5 segundos en pasos de 50ms)
    bool resident_claimed = false;
    for (int i = 0; i < 30; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (verify_root_access(pid))
        {
            info.status = JailbreakStatus::Escaped;
            info.root_access = true;
            info.daemon_type = "Lapy JB Daemon / OnionHEN / etaHEN (Residente)";
            info.status_message =
                is_es ? "Jailbreak Concedido por Demonio Residente"
                      : "Jailbreak Granted by Resident Daemon";
            (void)unlink(kEtahenJailbreakPath);
            (void)unlink(kElevateProcPath);
            (void)unlink(kLapyResultPath);
            return info;
        }

        // Si el demonio consumió alguno de los archivos marcadores, no sobreescribir ni lanzar otro helper
        const bool marker_etahen_gone = (access(kEtahenJailbreakPath, F_OK) != 0);
        const bool marker_elevate_gone = (access(kElevateProcPath, F_OK) != 0);
        if (marker_etahen_gone || marker_elevate_gone)
        {
            resident_claimed = true;
            break;
        }
    }

    // 4. Si el demonio residente reclamó el marcador, darle un periodo de gracia adicional (hasta 1.5s)
    if (resident_claimed)
    {
        for (int i = 0; i < 30; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (verify_root_access(pid))
            {
                info.status = JailbreakStatus::Escaped;
                info.root_access = true;
                info.daemon_type = "Lapy JB Daemon / etaHEN";
                info.status_message =
                    is_es ? "Jailbreak Concedido Exitosamente" : "Jailbreak Granted Successfully";
                (void)unlink(kEtahenJailbreakPath);
                (void)unlink(kElevateProcPath);
                (void)unlink(kLapyResultPath);
                return info;
            }
        }
    }

    // 5. Si no hay demonio residente activo, LANZAR DE FORMA AUTÓNOMA el demonio integrado
    (void)unlink(kEtahenJailbreakPath);
    (void)unlink(kElevateProcPath);

    const bool launched = auto_launch_embedded_daemon();
    if (launched)
    {
        info.daemon_type =
            is_es ? "Lapy JB Daemon (Auto-lanzado)" : "Lapy JB Daemon (Auto-launched)";

        // Pequeña pausa para permitir que el cargador inicialice el demonio
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Re-publicar peticiones para que el demonio recién arrancado las procese
        publish_atomic_request(kEtahenJailbreakPath, pid);
        publish_atomic_request(kElevateProcPath, pid);

        // Esperar a que el demonio auto-lanzado complete la elevación (hasta 2.5 segundos)
        for (int i = 0; i < 50; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (verify_root_access(pid))
            {
                info.status = JailbreakStatus::Escaped;
                info.root_access = true;
                info.status_message =
                    is_es ? "Jailbreak Concedido (Demonio Autónomo)"
                          : "Jailbreak Granted (Autonomous Daemon)";
                (void)unlink(kEtahenJailbreakPath);
                (void)unlink(kElevateProcPath);
                (void)unlink(kLapyResultPath);
                return info;
            }
        }
    }

    // 6. Verificación final de privilegios
    (void)unlink(kEtahenJailbreakPath);
    (void)unlink(kElevateProcPath);
    (void)unlink(kLapyResultPath);

    if (verify_root_access(pid))
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
        info.status_message = is_es ? "Sandbox Restringido (Sin respuesta de Daemon)"
                                    : "Sandbox Restricted (No Daemon Response)";
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
