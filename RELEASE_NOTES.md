# Release Notes: MacheteSVG v1.0.0 / Notas de la Versión v1.0.0

![MacheteSVG Banner](https://raw.githubusercontent.com/VenezuelanBiggie24/MacheteSVG-PS5-Save-Manager/main/docs/screenshots/banner.png)

---

## English Documentation

### Overview
MacheteSVG v1.0.0 is the initial public release of the native PlayStation 5 and PlayStation 4 Savegame Manager and Cloud Sync utility for jailbroken PlayStation 5 consoles.

Built with a hardware-accelerated OpenGL 4.6 SDF renderer, MacheteSVG delivers a fluid 60 FPS "Liquid Glass" interface paired with cryptographic savefile safety features designed to prevent save corruption (`CE-108255-1`) and enable auto-resigning across accounts.

---

### Visual Interface & Real Hardware Captures

| English Dashboard (1080p Render) | PS5 Hardware Capture (Real Console) |
| :---: | :---: |
| ![English Dashboard](https://raw.githubusercontent.com/VenezuelanBiggie24/MacheteSVG-PS5-Save-Manager/main/docs/screenshots/dashboard_english_1080p.png) | ![Real Hardware Dashboard](https://raw.githubusercontent.com/VenezuelanBiggie24/MacheteSVG-PS5-Save-Manager/main/docs/screenshots/ps5_real_hardware_dashboard.png) |

| Google Drive RFC 8628 Device Flow | Modal Location Selector (Real Console) |
| :---: | :---: |
| ![Google Drive Dialog](https://raw.githubusercontent.com/VenezuelanBiggie24/MacheteSVG-PS5-Save-Manager/main/docs/screenshots/google_drive_dialog_1080p.png) | ![Real Hardware Modal](https://raw.githubusercontent.com/VenezuelanBiggie24/MacheteSVG-PS5-Save-Manager/main/docs/screenshots/ps5_real_hardware_modal.png) |

---

### Key Capabilities

#### 1. Zero-Corruption Save Architecture
- **Keystone File Preservation:** Preserves console-specific `sce_sys/keystone` and `sce_sys/param.sfo` files during restoration. Only save payload containers (e.g., `data000.bin`) are updated, allowing clean auto-resigning of saves downloaded from other users or consoles.
- **Error CE-108255-1 Elimination:** Employs orderly `sceSaveDataUmount` operations, allowing the Prospero kernel to seal PFS containers cleanly.
- **Global vnode Flush:** Enforces atomic writes followed by `sync()` / `fsync()`, invalidating FreeBSD vnode caches so backwards-compatible PS4 games reload modified saves immediately without console restarts.
- **Dummy Save Safeguard:** Prompts users to run newly installed games once before restoring saves to generate genuine local hardware keys.

#### 2. Google Drive Cloud Synchronization
- **OAuth 2.0 Device Flow (RFC 8628):** Link your Google account securely without entering credentials on the console. Simply navigate to `https://www.google.com/device` and enter the code shown on screen.
- **Asynchronous Execution:** Upload and download operations run inside dedicated worker threads, ensuring the UI remains responsive at 60 FPS.
- **Storage Standard:** Backups are structured in cloud root directory `/MacheteSVG/[TitleID - Game Name]/`.

#### 3. Autonomous Sandbox Escape & Community Title ID Standard
- **Title ID `PPSA99001`:** Conforms to the community-adopted homebrew allocation scheme (`PPSA99xxx`) to avoid collisions with commercial PlayStation titles.
- **Embedded Lapy Daemon:** Packages `lapy_jb_daemon.elf` within the application. The app attempts automatic connection to the local ELF loader (`127.0.0.1:9021`) and triggers privilege elevation without requiring external payload injection.

#### 4. Internationalization Engine (i18n)
- **Automatic Language Detection:** Console language is detected at launch. Spanish consoles initialize in Spanish, while English and all other languages default to English.
- **On-the-Fly Toggle:** Press **L3** at any moment to switch instantly between English and Spanish.

---

### Controls Summary
- **D-Pad Up / Down:** Navigate installed games list.
- **Cross (X):** Confirm selection / Backup save data.
- **Square:** Restore save data from selected storage location.
- **Triangle:** Open Google Drive cloud synchronization.
- **L3 (Left Stick Click):** Toggle interface language (English / Spanish).
- **Circle:** Cancel dialog / Back.

---

### Distribution Packages & Integrity Checksums

| File | Size | SHA-256 Checksum |
| :--- | :--- | :--- |
| `MacheteSVG_v1.0.0_PS5.zip` | 58.8 MB (61,695,071 bytes) | `5b2c038a75fbee56859330c96993a0a44c06ea74fba658add4ac29233f727978` |

All package files are validated with open-mode permissions (`0777`) for out-of-the-box execution on jailbroken PlayStation 5 systems.

---

### Installation Instructions
1. Download `MacheteSVG_v1.0.0_PS5.zip` from the release assets.
2. Extract the archive contents (`PPSA99001` folder).
3. Connect to your PS5 via FTP or use a file manager homebrew.
4. Upload the `PPSA99001` folder to `/data/homebrew/` or `/user/app/`.
5. Launch MacheteSVG from your homebrew loader (Itemzflow, ProsperoStore, or etaHEN menu).

---

## Notas de la Versión en Español (Spanish Documentation)

### Resumen General
MacheteSVG v1.0.0 es la versión pública inicial del administrador de partidas guardadas de PlayStation 5 y PlayStation 4 con sincronización en la nube para consolas PlayStation 5 con jailbreak.

Desarrollado con renderizado acelerado por hardware OpenGL 4.6 SDF, MacheteSVG ofrece una interfaz fluida a 60 FPS con diseño "Liquid Glass" y mecanismos de seguridad criptográfica diseñados para evitar la corrupción de datos (`CE-108255-1`) y permitir el resignado automático entre diferentes perfiles de usuario.

---

### Capturas de Pantalla y Hardware Real

| Panel Principal en Inglés (Render 1080p) | Captura de Hardware Real en PS5 |
| :---: | :---: |
| ![Panel en Inglés](https://raw.githubusercontent.com/VenezuelanBiggie24/MacheteSVG-PS5-Save-Manager/main/docs/screenshots/dashboard_english_1080p.png) | ![Panel en Consola Real](https://raw.githubusercontent.com/VenezuelanBiggie24/MacheteSVG-PS5-Save-Manager/main/docs/screenshots/ps5_real_hardware_dashboard.png) |

| Autorización Google Drive RFC 8628 | Selector Modal de Ubicación (Consola Real) |
| :---: | :---: |
| ![Autorización Google Drive](https://raw.githubusercontent.com/VenezuelanBiggie24/MacheteSVG-PS5-Save-Manager/main/docs/screenshots/google_drive_dialog_1080p.png) | ![Selector Modal](https://raw.githubusercontent.com/VenezuelanBiggie24/MacheteSVG-PS5-Save-Manager/main/docs/screenshots/ps5_real_hardware_modal.png) |

---

### Capacidades Principales

#### 1. Arquitectura de Guardado con Cero Corrupción
- **Preservación del Archivo Keystone:** Mantiene intactos los archivos de consola `sce_sys/keystone` y `sce_sys/param.sfo` durante la restauración. Solo se actualiza el contenedor de datos (`data000.bin`), permitiendo resignar partidas descargadas de otros usuarios sin rechazo del sistema.
- **Eliminación del Error CE-108255-1:** Realiza un desmontaje ordenado mediante `sceSaveDataUmount`, permitiendo que el kernel de Prospero selle los contenedores PFS de forma limpia.
- **Vaciado de Caché vnode:** Aplica escrituras atómicas junto con `sync()` / `fsync()`, invalidando las cachés de vnodes de FreeBSD para que los juegos de PS4 en retrocompatibilidad carguen las partidas de inmediato sin reiniciar la consola.
- **Protección de Partida Base:** Avisa al usuario para iniciar una vez cualquier juego nuevo y generar las claves criptográficas locales legítimas.

#### 2. Sincronización en la Nube con Google Drive
- **OAuth 2.0 Device Flow (RFC 8628):** Vincula tu cuenta de Google de forma segura sin ingresar contraseñas en la consola. Ingresa a `https://www.google.com/device` e introduce el código que se muestra en pantalla.
- **Ejecución Asíncrona:** Las transferencias se ejecutan en hilos dedicados para que la interfaz se mantenga a 60 FPS sin bloqueos.
- **Estructura Estándar:** Las copias de seguridad se organizan en la carpeta `/MacheteSVG/[TitleID - Nombre del Juego]/`.

#### 3. Escape Autónomo del Sandbox y Title ID Comunitario
- **Title ID `PPSA99001`:** Se adapta al estándar establecido por la comunidad de homebrew (`PPSA99xxx`) para evitar conflictos con identificadores de juegos oficiales.
- **Demonio Lapy Integrado:** Incluye `lapy_jb_daemon.elf` dentro del paquete. La aplicación se conecta de forma automática al cargador ELF local (`127.0.0.1:9021`) y solicita la elevación de privilegios sin depender de inyectores externos.

#### 4. Motor de Internacionalización (i18n)
- **Detección Automática de Idioma:** Detecta el idioma del sistema al iniciar. Las consolas en español abren en español; las consolas en inglés u otros idiomas abren en inglés.
- **Cambio Inmediato:** Presiona **L3** en cualquier momento para alternar al instante entre inglés y español.

---

### Resumen de Controles
- **D-Pad Arriba / Abajo:** Desplazarse por la lista de juegos.
- **Cruz (X):** Confirmar selección / Realizar copia de seguridad.
- **Cuadrado:** Restaurar partida guardada desde la ubicación seleccionada.
- **Triángulo:** Abrir sincronización en la nube con Google Drive.
- **L3 (Click Stick Izquierdo):** Alternar idioma de la interfaz (Inglés / Español).
- **Círculo:** Cancelar diálogo / Retroceder.

---

### Paquetes de Distribución y Sumas de Verificación

| Archivo | Tamaño | Suma de Verificación SHA-256 |
| :--- | :--- | :--- |
| `MacheteSVG_v1.0.0_PS5.zip` | 58.8 MB (61,695,071 bytes) | `5b2c038a75fbee56859330c96993a0a44c06ea74fba658add4ac29233f727978` |

Todos los archivos han sido generados y verificados con permisos de modo abierto (`0777`) para ejecución inmediata en consolas PlayStation 5.

---

### Instrucciones de Instalación
1. Descarga el archivo `MacheteSVG_v1.0.0_PS5.zip` desde los activos de la versión.
2. Descomprime el contenido del archivo (carpeta `PPSA99001`).
3. Conéctate a tu PS5 mediante FTP o utiliza un gestor de archivos homebrew.
4. Sube la carpeta `PPSA99001` a `/data/homebrew/` o `/user/app/`.
5. Ejecuta MacheteSVG desde tu cargador de homebrew habitual (Itemzflow, ProsperoStore o menú de etaHEN).
