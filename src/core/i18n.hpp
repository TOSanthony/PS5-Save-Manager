// MacheteSVG - Internationalization (i18n) Engine
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

namespace machete::i18n
{

enum class Language
{
    English = 0,
    Spanish = 1,
};

enum class StringId
{
    // Application Header
    AppTitle,
    AppSubtitle,
    JailbreakActiveRoot,
    SandboxRestricted,
    CloudConnected,
    CloudConnectPrompt,

    // Games List & Details
    GamesCount,
    ScanningGames,
    NoGamesFound,
    StatusPrefix,
    LastActivityPrefix,
    KeystoneStatusHeader,
    KeystoneStatusOk,
    KeystoneStatusPending,
    StorageLocationsHeader,
    SaveActionsHeader,

    // Storage Details & Buttons
    LocalStorageInfo,
    UsbStorageInfo,
    CloudStorageInfo,
    BtnBackup,
    BtnRestore,
    BtnCloudSync,

    // Footer Legend & DualSense Hints
    LegendSelect,
    LegendBack,
    LegendCloud,
    LegendBackup,
    LegendLanguage,
    HintTheme,
    HintNavigate,
    HintRefresh,
    HintLanguage,

    // Storage Devices & Hotplug
    StorageInternal,
    StorageM2,
    StorageUsbFormat,
    StorageCloud,
    UsbConnectedFormat,
    UsbDisconnectedFormat,

    // Standardized Storage Location Tags
    LocInternal,
    LocM2,
    LocUsb,
    LocCloud,

    // Action Modal Dialog
    DialogActionTitle,
    DialogBackupUsb,
    DialogRestoreUsb,
    DialogSyncCloud,
    DialogCancel,
    DialogBackupHeading,
    DialogRestoreHeading,
    DialogStructureSubtitle,

    // Google Drive Dialog
    DriveDialogTitle,
    DriveDialogSubtitle,
    DriveDialogLinked,
    DriveStep1,
    DriveStep2,
    DriveWaiting,
    DriveRequestingCode,
    DriveSyncCurrent,
    DriveRestoreCurrent,
    DriveSyncAll,
    DriveSignOut,
    DriveClose,

    // Notifications and Toasts
    ToastJailbreakGranted,
    ToastBackupSuccess,
    ToastBackupFailed,
    ToastRestoreSuccess,
    ToastRestoreFailed,
    ToastCloudSuccess,
    ToastCloudFailed,
    ToastScanUpdated,
    ToastLanguageSwitched,
    ToastUploadingToCloud,
    ToastStartingCloudSync,
    ToastCloudLoggedOut,
    ToastDownloadingFromCloud,
    ToastUploadingGeneric,

    // Count marker
    Count
};

// Initializes language from PS5 system settings (sceSystemServiceParamGetInt) or env.
// Non-Spanish systems automatically default to English.
void init();

// Returns the current active language
Language current_language();

// Sets language explicitly
void set_language(Language lang);

// Toggles between English and Spanish
void toggle_language();

// Returns localized string for the specified ID
const char *get(StringId id);

// Convenience inline function
inline const char *tr(StringId id)
{
    return get(id);
}

} // namespace machete::i18n
