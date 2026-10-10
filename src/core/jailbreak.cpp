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

#if defined(__PROSPERO__) || defined(__Prospero__) || defined(__PS5__)
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
    int sceNetInit(void);
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

    // 2. Prueba de acceso a directorios de sistema de PlayStation 5 (/user/home)
    DIR *dir_home = opendir("/user/home");
    if (dir_home)
    {
        closedir(dir_home);
        return true;
    }

    // 3. Prueba definitiva de lectura/escritura Y listado bajo /data (patrón ProsperoStore)
    DIR *dir_data = opendir("/data");
    if (dir_data)
    {
        closedir(dir_data);
        char probe_path[96];
        std::snprintf(probe_path, sizeof(probe_path), "/data/.machete_probe_%d", pid);
        (void)unlink(probe_path);

        const int fd = open(probe_path, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
        {
            close(fd);
            (void)unlink(probe_path);
            return true;
        }
    }

    return false;
}

// Publica la petición de elevación atómicamente
bool publish_atomic_request(const char *target_path, int pid) noexcept
{
    char body[64];
    const int body_len = std::snprintf(body, sizeof(body), "{\"PID\": %d}\n", pid);
    if (body_len <= 0)
        return false;

    (void)unlink(target_path);
    const int fd = open(target_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;

    const bool ok = write_all_fd(fd, body, static_cast<std::size_t>(body_len));
    close(fd);
    (void)chmod(target_path, 0666);
    return ok;
}

} // namespace

void clean_shadowmount_interference() noexcept
{
    // Limpiar de forma segura cualquier rastro de PPSA99001 en listas blancas de etaHEN y ShadowMountPlus.
    // Las listas blancas activan el auto-pausado de Kstuff (kstuff_game_auto_toggle=1), lo cual detiene
    // la ejecucion de homebrews y provoca el error "Se cerro la app antes de que se pausara kstuff".
    const char *whitelist_files[] = {
        "/data/whitelist.txt",
        "/data/etaHEN/whitelist.txt",
        "/data/shadowmount/autotune.ini",
        "/data/shadowmount/config.ini",
    };

    for (const char *path : whitelist_files)
    {
        std::FILE *f = std::fopen(path, "r");
        if (!f)
            continue;

        std::vector<std::string> clean_lines;
        char line[512];
        bool modified = false;

        while (std::fgets(line, sizeof(line), f))
        {
            if (std::strstr(line, "PPSA99001") != nullptr)
            {
                modified = true;
                continue;
            }
            clean_lines.push_back(line);
        }
        std::fclose(f);

        if (modified)
        {
            std::FILE *out = std::fopen(path, "w");
            if (out)
            {
                for (const auto &l : clean_lines)
                    std::fputs(l.c_str(), out);
                std::fclose(out);
                (void)chmod(path, 0666);
            }
        }
    }
}

bool auto_launch_embedded_daemon()
{
#if defined(__PROSPERO__) || defined(__Prospero__) || defined(__PS5__)
    static bool s_net_initialized = false;
    if (!s_net_initialized)
    {
        (void)sceNetInit();
        s_net_initialized = true;
    }

    // Obtener los datos del demonio: probar rutas locales o usar el binario embebido en memoria
    const char *daemon_paths[] = {
        "/app0/daemon/lapy_jb_daemon.elf",
        "/system_ex/app/PPSA99001/daemon/lapy_jb_daemon.elf",
        "/data/homebrew/PPSA99001/daemon/lapy_jb_daemon.elf",
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
        constexpr int timeout_us = 3'000'000;
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
        clean_shadowmount_interference();
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
        clean_shadowmount_interference();
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

    // 3. Sondeo para demonio residente activo (hasta 500ms en pasos de 50ms)
    bool resident_claimed = false;
    for (int i = 0; i < 10; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (verify_root_access(pid))
        {
            clean_shadowmount_interference();
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

    // 4. Si el demonio residente reclamó el marcador, darle un periodo de gracia adicional (hasta 750ms)
    if (resident_claimed)
    {
        for (int i = 0; i < 15; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (verify_root_access(pid))
            {
                clean_shadowmount_interference();
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
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Re-publicar peticiones para que el demonio recién arrancado las procese
        publish_atomic_request(kEtahenJailbreakPath, pid);
        publish_atomic_request(kElevateProcPath, pid);

        // Esperar a que el demonio auto-lanzado complete la elevación (hasta 1.5 segundos)
        for (int i = 0; i < 30; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (verify_root_access(pid))
            {
                clean_shadowmount_interference();
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
        clean_shadowmount_interference();
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
