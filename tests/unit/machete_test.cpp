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
    dev.connected = true;

    auto backup_res = machete::save::backup_game_save(games[0], dev);
    EXPECT_FALSE(backup_res.message.empty());

    auto restore_res = machete::save::restore_game_save(games[0], dev);
    EXPECT_FALSE(restore_res.message.empty());
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

} // namespace hui::testing
