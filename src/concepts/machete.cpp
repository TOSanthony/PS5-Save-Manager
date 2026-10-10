// MacheteSVG - PS5 & PS4 Savegame Manager & Cloud Sync
// Powered by ps5-homebrew-ui & PS5 Payload SDK
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/concepts.hpp"

#include "cloud/google_drive.hpp"
#include "core/game_scanner.hpp"
#include "core/i18n.hpp"
#include "core/jailbreak.hpp"
#include "core/save_manager.hpp"
#include "core/storage.hpp"
#include "core/tween.hpp"
#include "demo/catalog.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;

// Identificadores de elementos navegables
enum Item : int
{
    // Panel Izquierdo: Lista visible de juegos (hasta 5 en pantalla)
    kGame0 = 0,
    kGame1,
    kGame2,
    kGame3,
    kGame4,

    // Panel Derecho: Botones de acción
    kBtnBackup,
    kBtnRestore,
    kBtnCloud,

    // Cabecera: Botón / Chip de Google Drive
    kBtnCloudHeader,

    kItemCount
};

// Rectángulos de foco base
constexpr Rect kRects[kItemCount] = {
    // Lista de juegos (panel izquierdo)
    {116, 330, 640, 92},
    {116, 434, 640, 92},
    {116, 538, 640, 92},
    {116, 642, 640, 92},
    {116, 746, 640, 92},

    // Botones de acción (panel derecho)
    {836, 780, 260, 64},  // Respaldar (Primary)
    {1112, 780, 260, 64}, // Restaurar (Secondary)
    {836, 856, 536, 64},  // Sincronizar Google Drive

    // Chip Google Drive en Cabecera
    {1540, 108, 284, 44},
};

static constexpr std::array<const char *const, 4> kTechniques = {
    "Core-Shell Architecture: Complete decoupling between cryptographic engine and GUI",
    "Keystone Preservation: Protects /sce_sys/ to prevent error CE-108255-1",
    "Atomic POSIX Writes: Safe replacements with vnode memory synchronization",
    "Cloud Synchronization: Google Drive API via Device Authorization Grant",
};

class Machete final : public app::Concept
{
  public:
    explicit Machete(app::Context &context) : context_(context)
    {
        machete::i18n::init();
        focus_ = kGame0;
        selected_game_ = 0;
        focus_amount_[kGame0].snap(1.0f);
        build_tour();
    }

    ~Machete() override
    {
        if (jb_thread_.joinable())
            jb_thread_.join();
        machete::scanner::release_game_icon_textures(games_);
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "machete",
            "MacheteSVG Save Manager",
            "PS5 & PS4 Save Data Manager and Google Drive Sync",
            "src/concepts/machete.cpp",
            audio::SoundSet::glass,
            Color::rgb(0x76d6ff),
            kTechniques,
        };
        return kInfo;
    }

    void start_async_jailbreak()
    {
        if (jb_thread_running_.exchange(true))
            return;

        if (jb_thread_.joinable())
            jb_thread_.join();

        jb_thread_ = std::thread([this]() {
            const auto result = machete::jailbreak::request_jailbreak();
            {
                std::lock_guard<std::mutex> lock(jb_mutex_);
                jb_info_ = result;
            }
            if (result.root_access)
            {
                root_active_.store(true);
                jb_needs_refresh_.store(true);
            }
            jb_thread_running_.store(false);
        });
    }

    void enter() override
    {
        age_ = 0.0f;
        dialog_ = false;
        drive_dialog_ = false;
        notification_timer_ = 0.0f;
        storage_poll_timer_ = 0.0f;
        jb_poll_timer_ = 0.0f;

        // 1. Verificación instantánea sin bloqueo (< 1ms)
        jb_info_ = machete::jailbreak::check_status();
        root_active_.store(jb_info_.root_access);

        // 2. Escaneo de dispositivos de almacenamiento
        active_devices_ = machete::storage::scan_storage_devices();

        // 3. Escaneo dinámico de juegos y partidas
        refresh_games();

        // 4. Si aún no tenemos privilegios root, lanzar elevación en segundo plano
        if (!root_active_.load())
        {
            start_async_jailbreak();
        }
    }

    void refresh_games()
    {
        machete::scanner::release_game_icon_textures(games_);
        games_ = machete::scanner::scan_installed_games_and_saves();
        machete::scanner::load_game_icon_textures(games_);

        if (games_.empty())
        {
            selected_game_ = 0;
            scroll_offset_ = 0;
        }
        else
        {
            selected_game_ = std::clamp(selected_game_, 0, static_cast<int>(games_.size()) - 1);
            adjust_scroll();
        }
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        age_ += dt;
        clock_ += dt;
        if (notification_timer_ > 0.0f)
            notification_timer_ -= dt;

        // Sondeo periódico en segundo plano de inserción/extracción USB
        storage_poll_timer_ += dt;
        if (storage_poll_timer_ >= 1.2f)
        {
            storage_poll_timer_ = 0.0f;
            std::string hotplug_msg;
            if (machete::storage::poll_storage_changes(active_devices_, hotplug_msg))
            {
                std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                              hotplug_msg.c_str());
                notification_timer_ = 4.0f;
                feedback.play(audio::Cue::connect, 1.0f, 0.0f);
            }
        }

        // Notificación de elevación de privilegios completada en segundo plano
        if (jb_needs_refresh_.exchange(false))
        {
            refresh_games();
            const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);
            std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                          is_es ? "Jailbreak Concedido Exitosamente"
                                : "Jailbreak Granted Successfully");
            notification_timer_ = 4.0f;
            feedback.play(audio::Cue::welcome, 1.0f, 0.0f);
        }

        // Sondeo periódico no bloqueante de privilegios
        jb_poll_timer_ += dt;
        if (jb_poll_timer_ >= 3.0f)
        {
            jb_poll_timer_ = 0.0f;
            if (!root_active_.load() && !jb_thread_running_.load())
            {
                const auto current = machete::jailbreak::check_status();
                if (current.root_access)
                {
                    {
                        std::lock_guard<std::mutex> lock(jb_mutex_);
                        jb_info_ = current;
                    }
                    root_active_.store(true);
                    refresh_games();
                    notification_timer_ = 4.0f;
                    std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                                  machete::i18n::tr(machete::i18n::StringId::ToastJailbreakGranted));
                    feedback.play(audio::Cue::welcome, 1.0f, 0.0f);
                }
            }
        }

        const ui::Theme &theme = ui::themes()[static_cast<std::size_t>(theme_)];
        const int count = static_cast<int>(ui::themes().size());

        // L2 / R2: Cambio de tema en tiempo real
        if (input.is_pressed(Action::jump_next) || input.is_pressed(Action::jump_prev))
        {
            previous_theme_ = theme_;
            theme_ = (theme_ + (input.is_pressed(Action::jump_next) ? 1 : count - 1)) % count;
            switch_.start(context_.settings.reduced_motion ? 0.16f : 0.42f);
            feedback.play(audio::Cue::tab, 1.0f, 0.0f);
        }
        // Atajo Triangle: Abrir / alternar sincronización con Google Drive directamente
        else if (input.is_pressed(Action::north))
        {
            if (drive_dialog_)
            {
                drive_dialog_ = false;
                feedback.play(audio::Cue::modal_close, 1.0f, 0.0f);
            }
            else
            {
                open_google_drive_dialog(feedback);
            }
        }
        // Atajo Square: Recargar lista de juegos y reintentar elevación si es necesario
        else if (input.is_pressed(Action::west))
        {
            if (!root_active_.load())
                start_async_jailbreak();
            refresh_games();
            std::snprintf(notification_text_, sizeof(notification_text_),
                          machete::i18n::tr(machete::i18n::StringId::ToastScanUpdated), games_.size());
            notification_timer_ = 3.0f;
            feedback.play(audio::Cue::select, 1.0f, 0.0f);
        }
        // Atajo L3: Alternar Idioma (Inglés / Español)
        else if (input.is_pressed(Action::l3))
        {
            machete::i18n::toggle_language();
            std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                          machete::i18n::tr(machete::i18n::StringId::ToastLanguageSwitched));
            notification_timer_ = 2.5f;
            feedback.play(audio::Cue::select, 1.0f, 0.0f);
        }
        else if (drive_dialog_)
        {
            update_drive_dialog(input, feedback);
        }
        else if (dialog_)
        {
            update_dialog(input, feedback);
        }
        else
        {
            update_navigation(input, feedback);
            if (input.is_pressed(Action::confirm))
                activate(feedback);
        }

        // Animaciones e inercia física (Spring physics)
        const float omega = context_.settings.reduced_motion ? 60.0f : theme.omega;
        switch_.update(dt);

        for (int i = 0; i < kItemCount; ++i)
        {
            const bool focused = (!dialog_ && !drive_dialog_ && i == focus_);
            focus_amount_[i].target = focused ? 1.0f : 0.0f;
            focus_amount_[i].update(dt, std::max(omega, 22.0f));
            press_[i].update(dt, 9.0f);
        }

        dialog_show_.target = dialog_ ? 1.0f : 0.0f;
        dialog_show_.update(dt, std::min(omega, 26.0f));

        drive_dialog_show_.target = drive_dialog_ ? 1.0f : 0.0f;
        drive_dialog_show_.update(dt, std::min(omega, 26.0f));

        dialog_focus_value_.target = static_cast<float>(dialog_focus_);
        dialog_focus_value_.update(dt, std::max(omega, 20.0f));
        refuse_.update(dt, 9.0f);
    }

    void draw(app::Frame &frame) const override
    {
        const auto themes = ui::themes();
        const ui::Theme &now = themes[static_cast<std::size_t>(theme_)];
        const ui::Theme &old = themes[static_cast<std::size_t>(previous_theme_)];
        const float t = switch_.running ? switch_.progress() : 1.0f;

        const ui::Theme &behind = t < 0.5f ? old : now;
        frame.backdrop = behind.backdrop;
        frame.backdrop.time = clock_;

        if (behind.style == ui::SurfaceStyle::glass)
        {
            for (int i = 0; i < 4; ++i)
            {
                const float phase = clock_ * 0.12f + static_cast<float>(i) * 1.7f;
                frame.scene.circle(
                    480.0f + static_cast<float>(i) * 380.0f + std::sin(phase) * 120.0f,
                    560.0f + std::cos(phase * 1.3f) * 220.0f, 170.0f,
                    gfx::mix(behind.primary, behind.accent, static_cast<float>(i % 2))
                        .with_alpha(0.55f));
            }
        }

        draw_dashboard(frame.scene, now, frame.glass_texture, 1.0f, 0.0f);
    }

  private:
    void adjust_scroll()
    {
        const int total = static_cast<int>(games_.size());
        if (total <= 5)
        {
            scroll_offset_ = 0;
            return;
        }
        if (selected_game_ < scroll_offset_)
            scroll_offset_ = selected_game_;
        else if (selected_game_ >= scroll_offset_ + 5)
            scroll_offset_ = selected_game_ - 4;
        scroll_offset_ = std::clamp(scroll_offset_, 0, total - 5);
    }

    void update_navigation(const InputFrame &input, app::Feedback &feedback)
    {
        const int total_games = static_cast<int>(games_.size());

        // Navegación en el Panel Izquierdo (Lista de Juegos)
        if (focus_ >= kGame0 && focus_ <= kGame4)
        {
            if (input.nav == Direction::down)
            {
                if (selected_game_ < total_games - 1)
                {
                    selected_game_++;
                    adjust_scroll();
                    const int visible_idx = selected_game_ - scroll_offset_;
                    move_to(kGame0 + visible_idx, feedback);
                }
                else
                {
                    refuse(feedback, input);
                }
            }
            else if (input.nav == Direction::up)
            {
                if (selected_game_ > 0)
                {
                    selected_game_--;
                    adjust_scroll();
                    const int visible_idx = selected_game_ - scroll_offset_;
                    move_to(kGame0 + visible_idx, feedback);
                }
                else
                {
                    // Subir a la cabecera (Google Drive)
                    move_to(kBtnCloudHeader, feedback);
                }
            }
            else if (input.nav == Direction::right)
            {
                move_to(kBtnBackup, feedback);
            }
            else
            {
                refuse(feedback, input);
            }
        }
        // Navegación en la Cabecera (Google Drive Chip)
        else if (focus_ == kBtnCloudHeader)
        {
            if (input.nav == Direction::down)
            {
                move_to(kBtnBackup, feedback);
            }
            else if (input.nav == Direction::left)
            {
                const int visible_idx = std::clamp(selected_game_ - scroll_offset_, 0, 4);
                move_to(kGame0 + visible_idx, feedback);
            }
            else
            {
                refuse(feedback, input);
            }
        }
        // Navegación en el Panel Derecho (Botones de Acción)
        else
        {
            if (input.nav == Direction::left)
            {
                const int visible_idx = std::clamp(selected_game_ - scroll_offset_, 0, 4);
                move_to(kGame0 + visible_idx, feedback);
            }
            else if (focus_ == kBtnBackup)
            {
                if (input.nav == Direction::right)
                    move_to(kBtnRestore, feedback);
                else if (input.nav == Direction::down)
                    move_to(kBtnCloud, feedback);
                else if (input.nav == Direction::up)
                    move_to(kBtnCloudHeader, feedback);
                else
                    refuse(feedback, input);
            }
            else if (focus_ == kBtnRestore)
            {
                if (input.nav == Direction::left)
                    move_to(kBtnBackup, feedback);
                else if (input.nav == Direction::down)
                    move_to(kBtnCloud, feedback);
                else if (input.nav == Direction::up)
                    move_to(kBtnCloudHeader, feedback);
                else
                    refuse(feedback, input);
            }
            else if (focus_ == kBtnCloud)
            {
                if (input.nav == Direction::up)
                    move_to(kBtnBackup, feedback);
                else
                    refuse(feedback, input);
            }
        }
    }

    void move_to(int item, app::Feedback &feedback)
    {
        if (item == focus_)
            return;
        focus_ = item;
        feedback.play(audio::Cue::focus, 1.0f, ui::pan_for_x(kRects[item].cx()));
    }

    void refuse(app::Feedback &feedback, const InputFrame &input)
    {
        if (input.nav != Direction::none)
        {
            refuse_.trigger();
            feedback.play(audio::Cue::error, 1.0f, ui::pan_for_x(kRects[focus_].cx()));
        }
    }

    void activate(app::Feedback &feedback)
    {
        press_[focus_].trigger();

        if (focus_ >= kGame0 && focus_ <= kGame4)
        {
            feedback.play(audio::Cue::select, 1.0f, ui::pan_for_x(kRects[focus_].cx()));
        }
        else if (focus_ == kBtnBackup)
        {
            dialog_is_backup_ = true;
            dialog_ = true;
            dialog_focus_ = 0;
            feedback.play(audio::Cue::modal_open, 1.0f, ui::pan_for_x(kRects[focus_].cx()));
        }
        else if (focus_ == kBtnRestore)
        {
            dialog_is_backup_ = false;
            dialog_ = true;
            dialog_focus_ = 0;
            feedback.play(audio::Cue::modal_open, 1.0f, ui::pan_for_x(kRects[focus_].cx()));
        }
        else if (focus_ == kBtnCloud || focus_ == kBtnCloudHeader)
        {
            open_google_drive_dialog(feedback);
        }
    }

    void open_google_drive_dialog(app::Feedback &feedback)
    {
        drive_dialog_ = true;
        drive_dialog_focus_ = 0;
        auto &drive = machete::cloud::GoogleDrive::instance();

        if (!drive.is_authorized())
        {
            drive.start_device_auth([](const machete::cloud::DeviceCodeInfo &) {
                // Notificación o actualización de código en vivo
            });
        }
        feedback.play(audio::Cue::modal_open, 1.0f, 0.0f);
    }

    void update_drive_dialog(const InputFrame &input, app::Feedback &feedback)
    {
        auto &drive = machete::cloud::GoogleDrive::instance();
        const bool authorized = drive.is_authorized();
        const int max_focus = authorized ? 3 : 1;

        if (input.nav == Direction::down)
        {
            if (drive_dialog_focus_ < max_focus)
            {
                drive_dialog_focus_++;
                feedback.play(audio::Cue::focus, 1.0f, 0.0f);
            }
        }
        else if (input.nav == Direction::up)
        {
            if (drive_dialog_focus_ > 0)
            {
                drive_dialog_focus_--;
                feedback.play(audio::Cue::focus, 1.0f, 0.0f);
            }
        }

        if (input.is_pressed(Action::back))
        {
            drive_dialog_ = false;
            feedback.play(audio::Cue::modal_close, 1.0f, 0.0f);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (!authorized)
            {
                // Botón Cerrar
                drive_dialog_ = false;
                feedback.play(audio::Cue::modal_close, 1.0f, 0.0f);
            }
            else
            {
                if (drive_dialog_focus_ == 0)
                {
                    // Sincronizar juego seleccionado
                    if (!games_.empty())
                    {
                        const auto &game = games_[static_cast<std::size_t>(selected_game_)];
                        drive.backup_save(game.title_id, game.title, game.save_path,
                                          [this](bool /*ok*/, const std::string &msg) {
                                              std::snprintf(notification_text_,
                                                            sizeof(notification_text_), "%s",
                                                            msg.c_str());
                                              notification_timer_ = 4.0f;
                                          });
                        std::snprintf(notification_text_, sizeof(notification_text_),
                                      machete::i18n::tr(machete::i18n::StringId::ToastUploadingToCloud),
                                      game.title.c_str());
                        notification_timer_ = 3.5f;
                    }
                    drive_dialog_ = false;
                    feedback.play(audio::Cue::saved, 1.0f, 0.0f);
                }
                else if (drive_dialog_focus_ == 1)
                {
                    // Sincronizar todas las partidas
                    std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                                  machete::i18n::tr(machete::i18n::StringId::ToastStartingCloudSync));
                    notification_timer_ = 4.0f;
                    drive_dialog_ = false;
                    feedback.play(audio::Cue::saved, 1.0f, 0.0f);
                }
                else if (drive_dialog_focus_ == 2)
                {
                    // Cerrar sesión
                    drive.logout();
                    std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                                  machete::i18n::tr(machete::i18n::StringId::ToastCloudLoggedOut));
                    notification_timer_ = 3.5f;
                    drive_dialog_ = false;
                    feedback.play(audio::Cue::modal_close, 1.0f, 0.0f);
                }
                else
                {
                    drive_dialog_ = false;
                    feedback.play(audio::Cue::modal_close, 1.0f, 0.0f);
                }
            }
        }
    }

    void update_dialog(const InputFrame &input, app::Feedback &feedback)
    {
        const int opt_count = static_cast<int>(active_devices_.size());
        if (input.nav == Direction::down)
        {
            if (dialog_focus_ < opt_count)
            {
                dialog_focus_++;
                feedback.play(audio::Cue::focus, 1.0f, 0.0f);
            }
        }
        else if (input.nav == Direction::up)
        {
            if (dialog_focus_ > 0)
            {
                dialog_focus_--;
                feedback.play(audio::Cue::focus, 1.0f, 0.0f);
            }
        }

        if (input.is_pressed(Action::back))
        {
            dialog_ = false;
            feedback.play(audio::Cue::modal_close, 1.0f, 0.0f);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (dialog_focus_ == opt_count) // Botón Cancelar
            {
                dialog_ = false;
                feedback.play(audio::Cue::modal_close, 1.0f, 0.0f);
            }
            else if (dialog_focus_ >= 0 && dialog_focus_ < opt_count && !games_.empty())
            {
                const auto &game = games_[static_cast<std::size_t>(selected_game_)];
                const auto &dev = active_devices_[static_cast<std::size_t>(dialog_focus_)];

                if (dev.type == machete::storage::StorageType::Cloud)
                {
                    auto &drive = machete::cloud::GoogleDrive::instance();
                    if (dialog_is_backup_)
                    {
                        drive.backup_save(game.title_id, game.title, game.save_path,
                                          [this](bool /*ok*/, const std::string &msg) {
                                              std::snprintf(notification_text_,
                                                            sizeof(notification_text_), "%s",
                                                            msg.c_str());
                                              notification_timer_ = 4.0f;
                                          });
                        std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                                      machete::i18n::tr(machete::i18n::StringId::ToastUploadingGeneric));
                    }
                    else
                    {
                        drive.restore_save("cloud_slot1_id", game.save_path,
                                           [this](bool /*ok*/, const std::string &msg) {
                                               std::snprintf(notification_text_,
                                                             sizeof(notification_text_), "%s",
                                                             msg.c_str());
                                               notification_timer_ = 4.0f;
                                           });
                        std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                                      machete::i18n::tr(machete::i18n::StringId::ToastDownloadingFromCloud));
                    }
                }
                else
                {
                    // Operación Real en Sistema de Archivos Local
                    if (dialog_is_backup_)
                    {
                        const auto res = machete::save::backup_game_save(game, dev);
                        std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                                      res.message.c_str());
                    }
                    else
                    {
                        const auto res = machete::save::restore_game_save(game, dev);
                        std::snprintf(notification_text_, sizeof(notification_text_), "%s",
                                      res.message.c_str());
                    }
                }

                notification_timer_ = 4.0f;
                dialog_ = false;
                feedback.play(audio::Cue::saved, 1.0f, 0.0f);
            }
        }
    }

    ui::Look look(int item) const
    {
        return {focus_amount_[item].value, press_[item].value, false};
    }

    void draw_dashboard(gfx::DrawList &list, const ui::Theme &theme, std::uint32_t glass,
                        float alpha, float slide) const
    {
        if (alpha <= 0.01f)
            return;
        const ui::Fonts &fonts = context_.fonts;
        ui::Painter paint(list, fonts, theme, glass);
        const float in = tween::cubic_out(age_ / 0.45f);

        list.push_opacity(alpha * in);
        list.push_transform(1.0f, 0, 0, 0, slide + 24.0f * (1.0f - in));

        // ==========================================
        // 1. CABECERA Y LOGO EMBLEMA (Apple Liquid Glass)
        // ==========================================
        const Rect logo_rect{96, 92, 60, 60};
        list.rounded_rect(logo_rect, 14.0f, Color::rgb(0x101b3d).with_alpha(0.85f));
        paint.stroke(logo_rect, 14.0f, 1.5f, Color::rgb(0x76d6ff).with_alpha(0.35f));

        // Silueta Memory Card y filo de machete
        list.rounded_rect({logo_rect.x + 16, logo_rect.y + 14, 28, 34}, 4.0f,
                          Color::rgb(0x76d6ff).with_alpha(0.25f));
        list.line(logo_rect.x + 12, logo_rect.y + 46, logo_rect.x + 48, logo_rect.y + 14, 3.0f,
                  Color::rgb(0x76d6ff));

        paint.heading(machete::i18n::tr(machete::i18n::StringId::AppTitle), 172, 138, 44, paint.page_text());
        paint.label(machete::i18n::tr(machete::i18n::StringId::AppSubtitle), 174, 166, 17, paint.page_text_muted());

        // Badge de Estado del Jailbreak / Daemon
        const Rect jb_chip{1190, 108, 330, 44};
        const bool is_root = root_active_.load();
        list.rounded_rect(jb_chip, 22.0f, Color::rgb(0x132242).with_alpha(0.8f));
        paint.stroke(jb_chip, 22.0f, 1.5f,
                     is_root ? Color::rgb(0x38ef7d).with_alpha(0.4f)
                             : Color::rgb(0xffaa00).with_alpha(0.4f));
        list.circle(jb_chip.x + 22, jb_chip.cy(), 5.0f,
                    is_root ? Color::rgb(0x38ef7d) : Color::rgb(0xffaa00));
        paint.body(is_root ? machete::i18n::tr(machete::i18n::StringId::JailbreakActiveRoot)
                           : machete::i18n::tr(machete::i18n::StringId::SandboxRestricted),
                   jb_chip.x + 36, jb_chip.cy() + 7, 18, Color::rgb(0xffffff));

        // Botón / Chip de Google Drive (Navegable y Enfocable)
        const Rect cloud_chip = kRects[kBtnCloudHeader];
        const bool cloud_focused = (focus_ == kBtnCloudHeader);
        const bool drive_auth = machete::cloud::GoogleDrive::instance().is_authorized();

        list.rounded_rect(cloud_chip, 22.0f,
                          cloud_focused ? Color::rgb(0x1d3b66).with_alpha(0.95f)
                                        : Color::rgb(0x132242).with_alpha(0.8f));
        paint.stroke(cloud_chip, 22.0f, cloud_focused ? 2.5f : 1.5f,
                     cloud_focused ? theme.primary
                                   : (drive_auth ? Color::rgb(0x38ef7d).with_alpha(0.5f)
                                                 : Color::rgb(0x76d6ff).with_alpha(0.3f)));
        if (cloud_focused)
            paint.focus_ring(cloud_chip, 22.0f, focus_amount_[kBtnCloudHeader].value);

        list.circle(cloud_chip.x + 22, cloud_chip.cy(), 5.0f,
                    drive_auth ? Color::rgb(0x38ef7d) : Color::rgb(0x76d6ff));
        paint.body(drive_auth ? machete::i18n::tr(machete::i18n::StringId::CloudConnected)
                              : machete::i18n::tr(machete::i18n::StringId::CloudConnectPrompt),
                   cloud_chip.x + 36, cloud_chip.cy() + 7, 18,
                   cloud_focused ? theme.primary : Color::rgb(0xffffff));

        // ==========================================
        // 2. PANEL IZQUIERDO: LISTA DINAMICA DE JUEGOS
        // ==========================================
        const Rect left_panel{96, 210, 680, 750};
        paint.panel(left_panel);

        paint.label((machete::i18n::current_language() == machete::i18n::Language::Spanish)
                        ? "PARTIDAS Y JUEGOS DETECTADOS"
                        : "DETECTED GAMES & SAVE DATA",
                    126, 256, 18, theme.text_muted);
        char count_str[32];
        std::snprintf(count_str, sizeof(count_str),
                      machete::i18n::tr(machete::i18n::StringId::GamesCount), games_.size());
        paint.label(count_str, 736, 256, 18, theme.primary, gfx::Align::right);

        const int total_games = static_cast<int>(games_.size());
        if (total_games == 0)
        {
            const Rect empty_box{126, 320, 620, 160};
            list.rounded_rect(empty_box, theme.radius, Color::rgb(0xffffff).with_alpha(0.04f));
            paint.stroke(empty_box, theme.radius, 1.0f, theme.outline);
            const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);
            paint.heading(is_es ? "No se detectaron juegos instalados"
                                : "No installed games detected",
                          empty_box.cx(), empty_box.cy() - 14, 24, theme.text, gfx::Align::center);
            paint.body(is_es ? "Verifica permisos o presiona Cuadrado para reescanear"
                             : "Verify permissions or press Square to re-scan",
                       empty_box.cx(), empty_box.cy() + 18, 18, theme.text_muted, gfx::Align::center);
        }

        const int visible_count = std::min(5, total_games);

        for (int i = 0; i < visible_count; ++i)
        {
            const int game_idx = scroll_offset_ + i;
            if (game_idx >= total_games)
                break;

            const auto &game = games_[static_cast<std::size_t>(game_idx)];
            const Rect r = kRects[kGame0 + i];
            const bool is_focused = (focus_ == kGame0 + i);
            const bool is_selected = (selected_game_ == game_idx);

            // Fondo de la fila
            if (is_focused)
            {
                paint.surface(r, theme.radius, theme.surface_high, theme.outline, 1.0f);
                paint.focus_ring(r, theme.radius, focus_amount_[kGame0 + i].value);
            }
            else if (is_selected)
            {
                list.rounded_rect(r, theme.radius, theme.primary.with_alpha(0.18f));
                list.line(r.x + 2, r.y + 12, r.x + 2, r.y + r.h - 12, 4.0f, theme.primary);
            }
            else
            {
                list.rounded_rect(r, theme.radius, Color::rgb(0xffffff).with_alpha(0.04f));
            }

            // Icono Real del juego (icon0.png) con fallback suave
            const Rect icon_rect{r.x + 14, r.y + 14, 64, 64};
            if (game.icon_texture != 0)
            {
                list.image(game.icon_texture, icon_rect, gfx::kFullUv, Color::rgb(0xffffff), 8.0f);
                paint.stroke(icon_rect, 8.0f, 1.0f, Color::rgb(0xffffff).with_alpha(0.2f));
            }
            else
            {
                // Tarjeta monocromática estilizada
                list.rounded_rect(icon_rect, 8.0f, Color::rgb(0x142b4d).with_alpha(0.85f));
                paint.stroke(icon_rect, 8.0f, 1.0f, Color::rgb(0x76d6ff).with_alpha(0.3f));
                char init[4] = "PS";
                if (!game.title.empty())
                {
                    init[0] = game.title[0];
                    init[1] = '\0';
                }
                paint.label(init, icon_rect.cx(), icon_rect.cy() + 7, 24, Color::rgb(0x76d6ff),
                            gfx::Align::center);
            }

            // Título oficial y metadatos
            paint.heading(game.title.c_str(), r.x + 92, r.y + 44, 24,
                          is_focused ? theme.text : theme.text.with_alpha(0.9f));
            char sub[96];
            std::snprintf(sub, sizeof(sub), "%s  -  %s  -  %s", game.platform.c_str(),
                          game.title_id.c_str(), game.size_str.c_str());
            paint.body(sub, r.x + 92, r.y + 70, 18, theme.text_muted);
        }

        // ==========================================
        // 3. PANEL DERECHO: DETALLES Y ACCIONES
        // ==========================================
        const Rect right_panel{806, 210, 1018, 750};
        paint.panel(right_panel);

        if (!games_.empty())
        {
            const auto &current_game = games_[static_cast<std::size_t>(selected_game_)];

            // Carátula / Icono grande (196x196)
            const Rect big_cover{836, 256, 196, 196};
            if (current_game.icon_texture != 0)
            {
                list.image(current_game.icon_texture, big_cover, gfx::kFullUv,
                           Color::rgb(0xffffff), 14.0f);
                paint.stroke(big_cover, 14.0f, 1.5f, Color::rgb(0xffffff).with_alpha(0.25f));
            }
            else
            {
                list.rounded_rect(big_cover, 14.0f, Color::rgb(0x142b4d).with_alpha(0.85f));
                paint.stroke(big_cover, 14.0f, 2.0f, Color::rgb(0x76d6ff).with_alpha(0.4f));
                char init[4] = "PS";
                if (!current_game.title.empty())
                {
                    init[0] = current_game.title[0];
                    init[1] = '\0';
                }
                paint.heading(init, big_cover.cx(), big_cover.cy() + 20, 68,
                              Color::rgb(0x76d6ff), gfx::Align::center);
            }

            // Metadatos principales
            const Rect tag_rect{1058, 256, 110, 30};
            list.rounded_rect(tag_rect, 6.0f, Color::rgb(0x194582).with_alpha(0.7f));
            paint.label(current_game.platform.c_str(), tag_rect.cx(), tag_rect.cy() + 6, 16,
                        Color::rgb(0x76d6ff), gfx::Align::center);

            paint.label(current_game.title_id.c_str(), 1184, 276, 18, theme.text_muted);
            paint.heading(current_game.title.c_str(), 1058, 334, 38, theme.text);

            char slot_info[80];
            std::snprintf(slot_info, sizeof(slot_info),
                          machete::i18n::tr(machete::i18n::StringId::StatusPrefix),
                          current_game.size_str.c_str());
            paint.body(slot_info, 1058, 372, 22, theme.text_muted);

            char save_time[80];
            std::snprintf(save_time, sizeof(save_time),
                          machete::i18n::tr(machete::i18n::StringId::LastActivityPrefix),
                          current_game.timestamp_str.c_str());
            paint.body(save_time, 1058, 404, 20, theme.primary);

            // Separador
            list.line(836, 480, 1784, 480, 1.0f, theme.text_muted.with_alpha(0.2f));

            // Estado del Keystone
            paint.label(machete::i18n::tr(machete::i18n::StringId::KeystoneStatusHeader),
                        836, 520, 17, theme.text_muted);
            list.rounded_rect({836, 536, 948, 54}, 8.0f, Color::rgb(0x142b1f).with_alpha(0.6f));
            list.circle(860, 563, 6.0f,
                        current_game.has_keystone ? Color::rgb(0x38ef7d) : Color::rgb(0xffaa00));
            paint.body(current_game.has_keystone
                           ? machete::i18n::tr(machete::i18n::StringId::KeystoneStatusOk)
                           : machete::i18n::tr(machete::i18n::StringId::KeystoneStatusPending),
                       880, 570, 20, Color::rgb(0xa3ffcc));

            // Destinos de almacenamiento disponibles
            paint.label(machete::i18n::tr(machete::i18n::StringId::StorageLocationsHeader),
                        836, 630, 17, theme.text_muted);
            const char *locs[] = {
                machete::i18n::tr(machete::i18n::StringId::LocInternal),
                machete::i18n::tr(machete::i18n::StringId::LocM2),
                machete::i18n::tr(machete::i18n::StringId::LocUsb),
                machete::i18n::tr(machete::i18n::StringId::LocCloud),
            };
            for (int l = 0; l < 4; ++l)
            {
                const Rect lr{836.0f + static_cast<float>(l % 2) * 480.0f,
                              646.0f + static_cast<float>(l / 2) * 44.0f, 460.0f, 36.0f};
                list.rounded_rect(lr, 6.0f, Color::rgb(0xffffff).with_alpha(0.05f));
                paint.body(locs[l], lr.x + 14, lr.cy() + 7, 18, theme.text_muted);
            }

            // Botones de acción
            paint.label(machete::i18n::tr(machete::i18n::StringId::SaveActionsHeader),
                        836, 756, 17, theme.text_muted);
            paint.button(kRects[kBtnBackup], machete::i18n::tr(machete::i18n::StringId::BtnBackup),
                         ui::ButtonKind::primary, look(kBtnBackup));
            paint.button(kRects[kBtnRestore], machete::i18n::tr(machete::i18n::StringId::BtnRestore),
                         ui::ButtonKind::secondary, look(kBtnRestore));
            paint.button(kRects[kBtnCloud], machete::i18n::tr(machete::i18n::StringId::BtnCloudSync),
                         ui::ButtonKind::secondary, look(kBtnCloud));
        }
        else
        {
            const bool is_es = (machete::i18n::current_language() == machete::i18n::Language::Spanish);
            paint.heading(is_es ? "Estado de MacheteSVG y Almacenamiento" : "MacheteSVG System & Storage",
                          856, 280, 32, theme.text);
            paint.body(is_es ? "El gestor de partidas nativo para PS5 y PS4 esta listo para operar."
                             : "Native save data manager for PS5 & PS4 is ready to operate.",
                       856, 324, 20, theme.text_muted);

            const char *guide_steps[] = {
                is_es ? "1. Privilegios: Observa el chip superior (Root / Sandbox)."
                      : "1. Privileges: Check upper badge (Root / Sandbox).",
                is_es ? "2. Deteccion: Con permisos root, lee automaticamente /user/home y /user/app."
                      : "2. Detection: With root privileges, scans /user/home and /user/app automatically.",
                is_es ? "3. Pendrive USB: Conecta un dispositivo exFAT para respaldos portables."
                      : "3. USB Drive: Connect an exFAT device for portable offline backups.",
                is_es ? "4. Google Drive: Presiona Triangulo para sincronizacion en la nube."
                      : "4. Google Drive: Press Triangle for cloud synchronization.",
                is_es ? "5. Reescanear: Presiona Cuadrado para refrescar los juegos y partidas."
                      : "5. Refresh: Press Square to re-scan games and save data.",
            };
            for (int s = 0; s < 5; ++s)
            {
                const Rect sr{856, 370.0f + static_cast<float>(s) * 56.0f, 918, 46};
                list.rounded_rect(sr, 8.0f, Color::rgb(0xffffff).with_alpha(0.04f));
                list.circle(sr.x + 20, sr.cy(), 5.0f, theme.primary);
                paint.body(guide_steps[s], sr.x + 36, sr.cy() + 6, 18, theme.text);
            }
        }

        // Toast de notificación
        if (notification_timer_ > 0.0f)
        {
            const Rect toast{560, 40, 800, 54};
            list.rounded_rect(toast, 27.0f, Color::rgb(0x0e271a).with_alpha(0.95f));
            paint.stroke(toast, 27.0f, 1.5f, Color::rgb(0x38ef7d).with_alpha(0.6f));
            paint.body(notification_text_, toast.cx(), toast.cy() + 7, 20, Color::rgb(0xffffff),
                       gfx::Align::center);
        }

        // ==========================================
        // 4. DIALOGO MODAL: GOOGLE DRIVE (DEVICE FLOW)
        // ==========================================
        const float drive_show = drive_dialog_show_.value;
        if (drive_show > 0.01f)
        {
            list.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0,
                              Color::rgb(0x000000).with_alpha(0.7f * drive_show));
            list.push_opacity(tween::clamp01(drive_show));
            list.push_transform(0.94f + 0.06f * tween::clamp01(drive_show), 960, 540, 0, 0);

            const Rect dlg{520, 210, 880, 640};
            paint.panel(dlg);

            auto &drive = machete::cloud::GoogleDrive::instance();
            const bool auth = drive.is_authorized();

            if (!auth)
            {
                paint.heading(machete::i18n::tr(machete::i18n::StringId::DriveDialogTitle),
                              570, 275, 36, theme.text);
                paint.body(machete::i18n::tr(machete::i18n::StringId::DriveDialogSubtitle),
                           570, 315, 21, theme.text_muted);

                // Paso 1: URL
                paint.label(machete::i18n::tr(machete::i18n::StringId::DriveStep1), 570, 365, 19,
                            theme.text_muted);
                const Rect url_box{570, 380, 780, 50};
                list.rounded_rect(url_box, 8.0f, Color::rgb(0x122744).with_alpha(0.85f));
                paint.stroke(url_box, 8.0f, 1.5f, Color::rgb(0x76d6ff).with_alpha(0.4f));
                paint.heading("https://www.google.com/device", url_box.cx(), url_box.cy() + 10, 24,
                              Color::rgb(0x76d6ff), gfx::Align::center);

                // Paso 2: Código de usuario
                paint.label(machete::i18n::tr(machete::i18n::StringId::DriveStep2), 570, 465, 19,
                            theme.text_muted);
                const Rect code_box{570, 480, 780, 66};
                list.rounded_rect(code_box, 8.0f, Color::rgb(0x0e3629).with_alpha(0.9f));
                paint.stroke(code_box, 8.0f, 2.0f, Color::rgb(0x38ef7d).with_alpha(0.7f));
                const std::string ucode =
                    drive.user_code().empty()
                        ? machete::i18n::tr(machete::i18n::StringId::DriveRequestingCode)
                        : drive.user_code();
                paint.heading(ucode.c_str(), code_box.cx(), code_box.cy() + 14, 38,
                              Color::rgb(0x38ef7d), gfx::Align::center);

                // Estado de espera o error de red
                if (drive.auth_state() == machete::cloud::AuthState::Error)
                {
                    list.circle(580, 580, 6.0f, Color::rgb(0xff5555));
                    const std::string err = drive.last_error().empty()
                                                ? "Fallo al conectar con Google. Verifica Internet."
                                                : drive.last_error();
                    paint.body(err.c_str(), 600, 587, 18, Color::rgb(0xff8888));
                }
                else
                {
                    const float pulse = (std::sin(clock_ * 4.0f) + 1.0f) * 0.5f;
                    list.circle(580, 580, 6.0f,
                                Color::rgb(0x76d6ff).with_alpha(0.4f + 0.6f * pulse));
                    paint.body(machete::i18n::tr(machete::i18n::StringId::DriveWaiting), 600, 587, 20,
                               theme.text_muted);
                }

                const Rect close_btn{570, 620, 780, 54};
                ui::Look close_look;
                close_look.focus = 1.0f;
                paint.button(close_btn, machete::i18n::tr(machete::i18n::StringId::DriveClose),
                             ui::ButtonKind::secondary, close_look);
            }
            else
            {
                paint.heading(machete::i18n::tr(machete::i18n::StringId::CloudConnected),
                              570, 280, 36, theme.text);
                paint.body(machete::i18n::tr(machete::i18n::StringId::DriveDialogLinked),
                           570, 320, 21, theme.text_muted);

                const char *drive_opts[] = {
                    machete::i18n::tr(machete::i18n::StringId::DriveSyncCurrent),
                    machete::i18n::tr(machete::i18n::StringId::DriveSyncAll),
                    machete::i18n::tr(machete::i18n::StringId::DriveSignOut),
                    machete::i18n::tr(machete::i18n::StringId::DriveClose),
                };

                for (int s = 0; s < 4; ++s)
                {
                    const Rect opt_rect{570, 370.0f + static_cast<float>(s) * 62.0f, 780, 52};
                    const bool is_focused = (drive_dialog_focus_ == s);

                    if (is_focused)
                    {
                        paint.surface(opt_rect, theme.radius, theme.surface_high, theme.primary,
                                      1.0f);
                        paint.focus_ring(opt_rect, theme.radius, 1.0f);
                    }
                    else
                    {
                        list.rounded_rect(opt_rect, theme.radius,
                                          Color::rgb(0xffffff).with_alpha(0.06f));
                    }

                    list.circle(opt_rect.x + 24, opt_rect.cy(), 6.0f,
                                is_focused ? theme.primary : Color::rgb(0x38ef7d));
                    paint.body(drive_opts[s], opt_rect.x + 44, opt_rect.cy() + 7, 20,
                               is_focused ? theme.text : theme.text_muted);
                }
            }

            list.pop_transform();
            list.pop_opacity();
        }

        // ==========================================
        // 5. DIALOGO MODAL: SELECTOR DE UBICACION
        // ==========================================
        const float show = dialog_show_.value;
        if (show > 0.01f)
        {
            list.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0,
                              Color::rgb(0x000000).with_alpha(0.65f * show));
            list.push_opacity(tween::clamp01(show));
            list.push_transform(0.94f + 0.06f * tween::clamp01(show), 960, 540, 0, 0);

            const Rect dlg{520, 240, 880, 580};
            paint.panel(dlg);

            paint.heading(dialog_is_backup_
                              ? machete::i18n::tr(machete::i18n::StringId::DialogBackupHeading)
                              : machete::i18n::tr(machete::i18n::StringId::DialogRestoreHeading),
                          570, 310, 36, theme.text);
            paint.body(machete::i18n::tr(machete::i18n::StringId::DialogStructureSubtitle),
                       570, 350, 21, theme.text_muted);

            const int opt_count = static_cast<int>(active_devices_.size());
            const float item_h = 58.0f;
            for (int s = 0; s < opt_count; ++s)
            {
                const auto &dev = active_devices_[static_cast<std::size_t>(s)];
                const Rect opt_rect{570, 385.0f + static_cast<float>(s) * item_h, 780, 52};
                const bool is_focused = (dialog_focus_ == s);

                if (is_focused)
                {
                    paint.surface(opt_rect, theme.radius, theme.surface_high, theme.primary, 1.0f);
                    paint.focus_ring(opt_rect, theme.radius, 1.0f);
                }
                else
                {
                    list.rounded_rect(opt_rect, theme.radius,
                                      Color::rgb(0xffffff).with_alpha(0.06f));
                }

                list.circle(opt_rect.x + 24, opt_rect.cy(), 6.0f,
                            dev.connected ? (is_focused ? theme.primary : Color::rgb(0x38ef7d))
                                          : theme.text_muted);

                paint.body(dev.label.c_str(), opt_rect.x + 44, opt_rect.cy() + 7, 20,
                           is_focused ? theme.text : theme.text_muted);

                paint.label(dev.free_space_formatted().c_str(), opt_rect.x + opt_rect.w - 18,
                            opt_rect.cy() + 6, 16, is_focused ? theme.primary : theme.text_muted,
                            gfx::Align::right);
            }

            const Rect cancel_btn{570, 385.0f + static_cast<float>(opt_count) * item_h + 12.0f, 780,
                                  54};
            ui::Look cancel_look;
            cancel_look.focus = (dialog_focus_ == opt_count) ? 1.0f : 0.0f;
            paint.button(cancel_btn, machete::i18n::tr(machete::i18n::StringId::DialogCancel),
                         ui::ButtonKind::secondary, cancel_look);

            list.pop_transform();
            list.pop_opacity();
        }

        // ==========================================
        // 6. TECLAS Y CONTROLES DUALSENSE
        // ==========================================
        const ui::GlyphStyle glyphs = theme.dark ? ui::GlyphStyle::dark() : ui::GlyphStyle::light();
        const ui::Hint hints[] = {
            {ui::Button::l2, machete::i18n::tr(machete::i18n::StringId::HintTheme), ui::Button::r2},
            {ui::Button::dpad, machete::i18n::tr(machete::i18n::StringId::HintNavigate)},
            {ui::Button::cross, machete::i18n::tr(machete::i18n::StringId::LegendSelect)},
            {ui::Button::circle, machete::i18n::tr(machete::i18n::StringId::LegendBack)},
            {ui::Button::triangle, machete::i18n::tr(machete::i18n::StringId::LegendCloud)},
            {ui::Button::square, machete::i18n::tr(machete::i18n::StringId::HintRefresh)},
            {ui::Button::left_stick, machete::i18n::tr(machete::i18n::StringId::HintLanguage)},
        };
        ui::HintLayout layout;
        layout.cy = 1018.0f;
        ui::draw_hints(list, fonts, glyphs, hints, 7, 96, false, layout);

        list.pop_transform();
        list.pop_opacity();
    }

    void build_tour()
    {
        tour_.clear();
    }

    app::Context &context_;
    std::vector<app::TourStep> tour_;
    float age_ = 0.0f;
    float clock_ = 0.0f;
    int theme_ = 0;
    int previous_theme_ = 0;
    tween::Timer switch_;

    int focus_ = 0;
    int selected_game_ = 0;
    int scroll_offset_ = 0;

    std::array<tween::Spring, kItemCount> focus_amount_;
    std::array<ui::Pulse, kItemCount> press_;
    ui::Pulse refuse_;

    // Lista dinámica de juegos escaneados
    std::vector<machete::scanner::ScannedGame> games_;

    // Modales
    bool dialog_ = false;
    bool dialog_is_backup_ = true;
    int dialog_focus_ = 0;
    tween::Spring dialog_show_;
    tween::Spring dialog_focus_value_;

    bool drive_dialog_ = false;
    int drive_dialog_focus_ = 0;
    tween::Spring drive_dialog_show_;

    // Almacenamiento dinámico y Hotplug
    std::vector<machete::storage::StorageDevice> active_devices_;
    float storage_poll_timer_ = 0.0f;

    // Jailbreak Daemon y elevación en vivo
    machete::jailbreak::JailbreakInfo jb_info_;
    float jb_poll_timer_ = 0.0f;
    std::thread jb_thread_;
    std::atomic<bool> jb_thread_running_{false};
    std::atomic<bool> jb_needs_refresh_{false};
    std::atomic<bool> root_active_{false};
    std::mutex jb_mutex_;

    // Notificaciones
    float notification_timer_ = 0.0f;
    char notification_text_[256] = "";
};

} // namespace

std::unique_ptr<app::Concept> make_machete(app::Context &context)
{
    return std::make_unique<Machete>(context);
}

} // namespace hui::concepts
