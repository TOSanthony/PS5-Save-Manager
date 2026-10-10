# MacheteSVG Acknowledgments & Credits

MacheteSVG is built upon the collaborative spirit and pioneering achievements of talented developers in the PlayStation homebrew community. We express our deepest gratitude and recognition to the following projects and authors:

---

## English Attribution

### 1. Apollo Save Tool — Bucanero
* **Project:** [Apollo Save Tool](https://github.com/bucanero/apollo-ps4)
* **Contribution:** The gold standard for savegame management, `keystone` file preservation, decryption, and account re-signing across PlayStation consoles. MacheteSVG's corruption prevention and integrity verification follow the architectural principles established by Bucanero.

### 2. PS5 Lapy JB Daemon — Lapy & ArkSama
* **Project:** [PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon)
* **Contribution:** The standalone daemon for process privilege escalation (ucred bump) and sandbox escape via `/download0/etahen_jailbreak`. MacheteSVG embeds and autonomously launches this daemon so the console does not require external third-party payload injectors.

### 3. ProsperoStore — BlackBearReloaded & mpereiraesaa
* **Project:** [ProsperoStore](https://github.com/blackbearreloaded/ProsperoStore)
* **Contribution:** Pioneer of autonomous local ELF loader auto-launching (`127.0.0.1:9021`) and community Title ID conventions (`PPSA99xxx`).

### 4. etaHEN & OnionHEN Teams
* **Developers:** LightningMods, ChendoChap, Sleirsgoevy, Astrelsky, Illusion, and scene contributors.
* **Contribution:** Development of HEN environments (etaHEN and OnionHEN) for PlayStation 5, `kstuff` kernel patches, on-demand jailbreak APIs, and system infrastructure enabling native homebrew on modern firmwares.

### 5. ShadowMountPlus — Drakmor & VoidWhisper
* **Project:** [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)
* **Developers:** Drakmor (Lead developer of ShadowMountPlus) & VoidWhisper (Original ShadowMount creator)
* **Contribution:** Groundbreaking automated filesystem and PFS mounting architecture for jailbroken PS5, and techniques for mounting PFS and user partitions without Prospero vnode cache corruption, enabling reliable access and instant reload of save data for both PS5 native titles and backwards-compatible PS4 games.

### 6. KSystemLib
* **Contribution:** Low-level kernel interface and system library primitives for PlayStation 5 homebrew, facilitating robust syscall execution and system management within jailbroken Prospero environments.

### 7. ps5-payload-sdk — John Törnblom
* **Project:** [ps5-payload-sdk](https://github.com/ps5-payload-dev/pacbrew-packages)
* **Contribution:** Native PlayStation 5 toolchain, `prospero-lld` linker, Prospero/FreeBSD header definitions, and cross-compilation environment.

### 8. ps5-homebrew-ui — BlackBearReloaded
* **Project:** [ps5-homebrew-ui](https://github.com/BlackBearReloaded/ps5-homebrew-ui)
* **Contribution:** Native OpenGL hardware-accelerated user interface framework for PlayStation 5, featuring 60 FPS rendering, spring kinematics, complete DualSense controller support, and modern Liquid Glass aesthetic.

### 9. stb_image — Sean Barrett
* **Project:** [stb](https://github.com/nothings/stb)
* **Contribution:** Single-header C image library for zero-overhead decoding of PNG assets (`icon0.png`) to GPU textures.

---

## Reconocimientos en Español (Spanish Attribution)

### 1. Apollo Save Tool — Bucanero
* **Proyecto:** [Apollo Save Tool](https://github.com/bucanero/apollo-ps4)
* **Aportación:** Estándar de oro para la gestión, preservación de archivos `keystone`, desencriptación y reasignación de partidas guardadas de consolas PlayStation. La arquitectura de validación de integridad y prevención de corrupción de MacheteSVG sigue los principios arquitectónicos establecidos por Bucanero.

### 2. PS5 Lapy JB Daemon — Lapy & ArkSama
* **Proyecto:** [PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon)
* **Aportación:** Creación del demonio independiente de elevación de privilegios (ucred bump) y escape de sandbox mediante la interfaz `/download0/etahen_jailbreak`. MacheteSVG integra el demonio de forma autónoma para que la consola no dependa de payloads de terceros.

### 3. ProsperoStore — BlackBearReloaded & mpereiraesaa
* **Proyecto:** [ProsperoStore](https://github.com/blackbearreloaded/ProsperoStore)
* **Aportación:** Pioneros en el auto-lanzamiento autónomo a través del ELF loader local (`127.0.0.1:9021`) y la convención de Title IDs para la comunidad homebrew (`PPSA99xxx`).

### 4. etaHEN & OnionHEN Teams
* **Desarrolladores:** LightningMods, ChendoChap, Sleirsgoevy, Astrelsky, Illusion y colaboradores de la escena.
* **Aportación:** Desarrollo de los entornos HEN (etaHEN y OnionHEN) para PlayStation 5, parches de kernel `kstuff`, APIs de jailbreak on-demand e infraestructura que hace posible la ejecución de homebrew nativo en firmwares modernos.

### 5. ShadowMountPlus — Drakmor & VoidWhisper
* **Proyecto:** [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)
* **Desarrolladores:** Drakmor (Desarrollador principal de ShadowMountPlus) y VoidWhisper (Creador original de ShadowMount)
* **Aportación:** Arquitectura pionera de montaje automatizado de sistemas de archivos y contenedores PFS en PS5 con jailbreak, junto con técnicas de montaje de particiones de usuario sin corrupción de caché de vnodes en Prospero, resolviendo el acceso fiable y la recarga instantánea de datos de guardado tanto de títulos nativos de PS5 como de PS4 en retrocompatibilidad.

### 6. KSystemLib
* **Aportación:** Primitivas de biblioteca de sistema y llamadas de kernel de bajo nivel para aplicaciones homebrew en PlayStation 5, facilitando la ejecución segura de llamadas al sistema y la gestión en entornos Prospero con jailbreak.

### 7. ps5-payload-sdk — John Törnblom
* **Proyecto:** [ps5-payload-sdk](https://github.com/ps5-payload-dev/pacbrew-packages)
* **Aportación:** Cadena de herramientas (toolchain) para PS5, enlazador `prospero-lld`, definiciones de cabeceras de Prospero/FreeBSD y entorno de compilación cruzada.

### 8. ps5-homebrew-ui — BlackBearReloaded
* **Proyecto:** [ps5-homebrew-ui](https://github.com/BlackBearReloaded/ps5-homebrew-ui)
* **Aportación:** Framework de interfaz gráfica nativo para PlayStation 5, renderizado acelerado por hardware OpenGL a 60 FPS, física de resortes (Spring physics), soporte completo para el mando DualSense y diseño moderno Liquid Glass.

### 9. stb_image — Sean Barrett
* **Proyecto:** [stb](https://github.com/nothings/stb)
* **Aportación:** Biblioteca de cabecera única en C para decodificación eficiente y sin sobrecarga de imágenes PNG (`icon0.png`) a texturas GPU.

---

*MacheteSVG is distributed under the GNU General Public License v3.0 (GPL-3.0).*
