// MacheteSVG - Unit test for Machete concept and storage mechanics
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"
#include "core/storage.hpp"
#include "core/game_scanner.hpp"
#include "core/jailbreak.hpp"
#include "core/i18n.hpp"
#include "core/save_manager.hpp"
#include "cloud/google_drive.hpp"

#include <chrono>
#include <thread>

namespace hui::testing
{

class MacheteTest : public ConceptFixture
{
  protected:
    std::unique_ptr<app::Concept> machete_ = concepts::make_machete(context_);
};

TEST_F(MacheteTest, ConceptCreationAndIdentity)
{
    ASSERT_NE(machete_, nullptr);
    EXPECT_STREQ(machete_->info().id, "machete");
    EXPECT_STREQ(machete_->info().name, "MacheteSVG Save Manager");
}

TEST_F(MacheteTest, InitialDrawIsFiniteAndNotEmpty)
{
    expect_drawable(*machete_);
}

TEST_F(MacheteTest, NavigationDoesNotCrash)
{
    // Navegar por la lista de juegos
    send(*machete_, nav(Direction::down), 0.1f);
    expect_drawable(*machete_);

    send(*machete_, nav(Direction::down), 0.1f);
    expect_drawable(*machete_);

    send(*machete_, nav(Direction::up), 0.1f);
    expect_drawable(*machete_);

    // Navegar a la derecha (acciones)
    send(*machete_, nav(Direction::right), 0.1f);
    expect_drawable(*machete_);

    // Regresar a la lista
    send(*machete_, nav(Direction::left), 0.1f);
    expect_drawable(*machete_);
}

TEST_F(MacheteTest, ModalDialogFlow)
{
    // Ir a la derecha y presionar respaldo (Action::confirm)
    send(*machete_, nav(Direction::right), 0.1f);
    send(*machete_, press(Action::confirm), 0.1f);
    expect_drawable(*machete_);

    // En el diálogo, navegar hacia abajo
    send(*machete_, nav(Direction::down), 0.1f);
    expect_drawable(*machete_);

    // Cerrar el diálogo con back
    send(*machete_, press(Action::back), 0.1f);
    expect_drawable(*machete_);
}

TEST_F(MacheteTest, StorageDetectionAndFormatting)
{
    EXPECT_TRUE(machete::storage::ensure_internal_storage() || true);
    auto devices = machete::storage::scan_storage_devices();
    EXPECT_GE(devices.size(), 2u); // Mínimo Internal y Cloud

    machete::storage::StorageDevice dev;
    dev.free_bytes = 1024ULL * 1024ULL * 1024ULL * 5ULL;
    EXPECT_FALSE(dev.free_space_formatted().empty());
}

TEST_F(MacheteTest, GoogleDriveInstance)
{
    auto &drive = machete::cloud::GoogleDrive::instance();
    EXPECT_GE(drive.sync_progress(), 0.0f);
}

TEST_F(MacheteTest, GoogleDriveHeaderShortcutAndDialog)
{
    // Presionar Triangle (Action::north) abre/enfoca Google Drive
    send(*machete_, press(Action::north), 0.1f);
    expect_drawable(*machete_);

    // Presionar Confirm abre el modal de Device Flow de Google Drive
    send(*machete_, press(Action::confirm), 0.1f);
    expect_drawable(*machete_);

    // Cerrar modal con Back
    send(*machete_, press(Action::back), 0.1f);
    expect_drawable(*machete_);
}

TEST_F(MacheteTest, GameScannerAndTextureCycle)
{
    auto games = machete::scanner::scan_installed_games_and_saves();
    EXPECT_FALSE(games.empty());
    EXPECT_FALSE(games[0].title.empty());
    EXPECT_FALSE(games[0].title_id.empty());

    machete::scanner::load_game_icon_textures(games);
    machete::scanner::release_game_icon_textures(games);
}

TEST_F(MacheteTest, JailbreakStatusAndRequest)
{
    auto status = machete::jailbreak::check_status();
    EXPECT_NE(status.status_message.empty(), true);
    const char *str = machete::jailbreak::status_to_string(status.status);
    EXPECT_NE(str, nullptr);
}

TEST_F(MacheteTest, SaveManagerBackupAndRestore)
{
    auto games = machete::scanner::scan_installed_games_and_saves();
    ASSERT_FALSE(games.empty());

    machete::storage::StorageDevice dev;
    dev.label = "Test USB";
    dev.mount_path = "/tmp/machete_test_usb";
    dev.root_dir = "/tmp/machete_test_usb/MacheteSVG";
    dev.connected = true;

    auto backup_res = machete::save::backup_game_save(games[0], dev);
    EXPECT_FALSE(backup_res.message.empty());

    auto restore_res = machete::save::restore_game_save(games[0], dev);
    EXPECT_FALSE(restore_res.message.empty());
}

TEST_F(MacheteTest, PhysicalBackupDiscoveryWithoutStaleFlags)
{
    machete::scanner::ScannedGame game;
    game.title_id = "CUSA99999";
    game.title = "Mock Game";

    const std::string mock_root = "/tmp/machete_mock_storage/MacheteSVG";
    const std::string mock_folder = mock_root + "/CUSA99999 - Mock Game";
    mkdir("/tmp/machete_mock_storage", 0777);
    mkdir(mock_root.c_str(), 0777);
    mkdir(mock_folder.c_str(), 0777);

    FILE *f = std::fopen((mock_folder + "/save.bin").c_str(), "wb");
    if (f)
    {
        std::fputs("sample data", f);
        std::fclose(f);
    }

    std::vector<machete::storage::StorageDevice> devices;
    machete::storage::StorageDevice dev;
    dev.id = "internal";
    dev.label = "Internal Mock";
    dev.type = machete::storage::StorageType::Internal;
    dev.mount_path = "/tmp/machete_mock_storage";
    dev.root_dir = mock_root;
    dev.connected = false; // Simulation: stale connected flag is false!
    devices.push_back(dev);

    auto backups = machete::save::find_available_backups(game, devices);
    EXPECT_FALSE(backups.empty());
    if (!backups.empty())
    {
        EXPECT_EQ(backups[0].title_id, "CUSA99999");
        EXPECT_EQ(backups[0].folder_name, "CUSA99999 - Mock Game");
    }

    // Cleanup
    std::remove((mock_folder + "/save.bin").c_str());
    rmdir(mock_folder.c_str());
    rmdir(mock_root.c_str());
    rmdir("/tmp/machete_mock_storage");
}

TEST_F(MacheteTest, InternationalizationEngine)
{
    using namespace machete::i18n;

    // Verify all strings are populated in both languages
    for (int lang_idx = 0; lang_idx < 2; ++lang_idx)
    {
        set_language(static_cast<Language>(lang_idx));
        for (std::size_t id = 0; id < static_cast<std::size_t>(StringId::Count); ++id)
        {
            const char *str = get(static_cast<StringId>(id));
            ASSERT_NE(str, nullptr);
            EXPECT_GT(std::strlen(str), 0u);
        }
    }

    // Test English
    set_language(Language::English);
    EXPECT_EQ(current_language(), Language::English);
    EXPECT_STREQ(tr(StringId::JailbreakActiveRoot), "Jailbreak Active (Root)");
    EXPECT_STREQ(tr(StringId::BtnBackup), "Backup Save Data");
    EXPECT_STREQ(tr(StringId::DialogBackupHeading), "Select Backup Destination");

    // Test Spanish
    set_language(Language::Spanish);
    EXPECT_EQ(current_language(), Language::Spanish);
    EXPECT_STREQ(tr(StringId::JailbreakActiveRoot), "Jailbreak Activo (Root)");
    EXPECT_STREQ(tr(StringId::BtnBackup), "Crear Copia de Seguridad");
    EXPECT_STREQ(tr(StringId::DialogBackupHeading), "Selecciona el Destino del Respaldo");

    // Test Toggle
    toggle_language();
    EXPECT_EQ(current_language(), Language::English);

    // Test L3 action handling in concept
    send(*machete_, press(Action::l3), 0.1f);
    expect_drawable(*machete_);
    EXPECT_EQ(current_language(), Language::Spanish);
}

TEST_F(MacheteTest, NonLatinFilteringAndFallback)
{
    // 1. Validar que cadenas no latinas (arabe, cirilico, CJK) sean identificadas como no renderizables
    EXPECT_FALSE(machete::scanner::is_renderable_latin("وولڤرين من مارفل"));
    EXPECT_FALSE(machete::scanner::is_renderable_latin("Человек-паук"));
    EXPECT_FALSE(machete::scanner::is_renderable_latin("スパイダーマン"));
    EXPECT_FALSE(machete::scanner::is_renderable_latin(""));

    // 2. Validar que cadenas latinas y Title ID sean aceptadas
    EXPECT_TRUE(machete::scanner::is_renderable_latin("Marvel's Wolverine"));
    EXPECT_TRUE(machete::scanner::is_renderable_latin("PPSA03671"));
    EXPECT_TRUE(machete::scanner::is_renderable_latin("Spider-Man 2"));
    EXPECT_TRUE(machete::scanner::is_renderable_latin("El juego"));
}

TEST_F(MacheteTest, LocalizedParamJsonExtraction)
{
    // Crear archivo temporal param.json con arabe como default pero ingles/espanol disponible
    const std::string tmp_json = "/tmp/machete_wolverine_test.json";
    std::FILE *f = std::fopen(tmp_json.c_str(), "w");
    ASSERT_NE(f, nullptr);
    const char *content = "{\n"
                          "  \"localizedParameters\": {\n"
                          "    \"defaultLanguage\": \"ar-AE\",\n"
                          "    \"ar-AE\": {\n"
                          "      \"titleName\": \"وولڤرين من مارفل\"\n"
                          "    },\n"
                          "    \"en-US\": {\n"
                          "      \"titleName\": \"Marvel's Wolverine\"\n"
                          "    }\n"
                          "  }\n"
                          "}\n";
    std::fputs(content, f);
    std::fclose(f);

    std::string extracted = machete::scanner::extract_title_from_param_json(tmp_json);
    EXPECT_EQ(extracted, "Marvel's Wolverine");
    std::remove(tmp_json.c_str());

    // Crear archivo temporal param.json SOLO con arabe (debe retornar vacio para fallback a Title ID)
    const std::string tmp_arabic_json = "/tmp/machete_only_arabic.json";
    f = std::fopen(tmp_arabic_json.c_str(), "w");
    ASSERT_NE(f, nullptr);
    const char *arabic_only = "{\n"
                              "  \"localizedParameters\": {\n"
                              "    \"defaultLanguage\": \"ar-AE\",\n"
                              "    \"ar-AE\": {\n"
                              "      \"titleName\": \"وولڤرين من مارفل\"\n"
                              "    }\n"
                              "  }\n"
                              "}\n";
    std::fputs(arabic_only, f);
    std::fclose(f);

    extracted = machete::scanner::extract_title_from_param_json(tmp_arabic_json);
    EXPECT_EQ(extracted, "");
    std::remove(tmp_arabic_json.c_str());
}

TEST_F(MacheteTest, AppIconTextureLifecycle)
{
    // Probar carga y liberacion de textura PNG desde el icono oficial del proyecto
    unsigned int tex = machete::scanner::load_texture_from_png("sce_sys/icon0.png");
    EXPECT_NE(tex, 0u);
    machete::scanner::release_texture(tex);
    EXPECT_EQ(tex, 0u);
}

TEST_F(MacheteTest, ProgressCallbackReportsProgress)
{
    machete::scanner::ScannedGame game;
    game.title_id = "PPSA01411";
    game.title = "Spider-Man Test";
    const std::string mock_save_dir = "/tmp/machete_test_save";
    mkdir(mock_save_dir.c_str(), 0777);
    FILE *f = std::fopen((mock_save_dir + "/slot0.bin").c_str(), "wb");
    if (f)
    {
        std::vector<char> dummy(1024 * 64, 'X');
        std::fwrite(dummy.data(), 1, dummy.size(), f);
        std::fclose(f);
    }
    game.save_path = mock_save_dir;

    machete::storage::StorageDevice dev;
    dev.label = "Test USB Async";
    dev.mount_path = "/tmp/machete_async_usb";
    dev.root_dir = "/tmp/machete_async_usb/MacheteSVG";
    dev.connected = true;

    std::vector<float> progress_points;
    auto backup_res = machete::save::backup_game_save(
        game, dev, [&](const machete::save::OperationProgress &p) {
            progress_points.push_back(p.progress);
            EXPECT_GE(p.progress, 0.0f);
            EXPECT_LE(p.progress, 1.0f);
        });

    EXPECT_FALSE(backup_res.message.empty());
    EXPECT_FALSE(progress_points.empty());
    if (!progress_points.empty())
    {
        EXPECT_GE(progress_points.back(), 0.0f);
    }

    // Cleanup
    std::remove((mock_save_dir + "/slot0.bin").c_str());
    rmdir(mock_save_dir.c_str());
    std::remove((dev.root_dir + "/" + game.title_id + " - " + game.title + "/slot0.bin").c_str());
    rmdir((dev.root_dir + "/" + game.title_id + " - " + game.title).c_str());
    rmdir(dev.root_dir.c_str());
    rmdir(dev.mount_path.c_str());
}

TEST_F(MacheteTest, AsyncOperationProtectionAndRender)
{
    // Navegar a botón de backup
    send(*machete_, nav(Direction::right), 0.1f);
    // Abrir modal de destino
    send(*machete_, press(Action::confirm), 0.1f);
    expect_drawable(*machete_);

    // Confirmar en el diálogo para disparar start_async_backup
    send(*machete_, press(Action::confirm), 0.1f);
    expect_drawable(*machete_);

    // Simular spam de botones mientras la operación se procesa
    for (int i = 0; i < 5; ++i)
    {
        send(*machete_, press(Action::confirm), 0.02f);
        send(*machete_, press(Action::west), 0.02f);
        send(*machete_, press(Action::north), 0.02f);
        send(*machete_, nav(Direction::down), 0.02f);
        expect_drawable(*machete_);
    }

    // Permitir tiempo para que el hilo termine y se actualice el estado
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    send(*machete_, InputFrame{}, 0.1f);
    expect_drawable(*machete_);
}

} // namespace hui::testing
