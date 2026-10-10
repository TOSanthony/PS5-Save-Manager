#!/usr/bin/env bash
# MacheteSVG - Packaging and Distribution Script for Jailbroken PS5
# Copyright (C) 2026 MacheteSVG Team
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
title_id="PPSA99001"
app_name="MacheteSVG"
version="1.0.0"

echo "=================================================="
echo "   Empaquetador de Distribución de MacheteSVG     "
echo "=================================================="

# 1. Validar recursos
echo "==> [1/4] Validando activos de presentación..."
bash "$root/tools/validate-assets.sh"

echo "==> [2/4] Validando pistas de audio..."
python3 "$root/tools/audio-check.py"

# 2. Compilar aplicación nativa (eboot.bin + sce_module) si no está construida
if [[ ! -f "$root/dist/$title_id/eboot.bin" ]]; then
    echo "==> [3/5] Compilando binario nativo PS5 (eboot.bin)..."
    PATH="/opt/homebrew/bin:/opt/homebrew/opt/coreutils/libexec/gnubin:/opt/homebrew/opt/llvm/bin:$PATH" \
    PS5_CLANG=/opt/homebrew/opt/llvm/bin/clang \
    HOST_CXX=/opt/homebrew/opt/llvm/bin/clang++ \
    CXX=/opt/homebrew/opt/llvm/bin/clang++ \
    make -C "$root" app
fi

dist_dir="$root/dist/$title_id"
[[ -f "$dist_dir/eboot.bin" ]] || {
    echo "Error crítico: eboot.bin no fue generado en $dist_dir/eboot.bin" >&2
    exit 1
}

echo "==> [4/5] Ensamblando recursos adicionales y guía técnica..."
cp "$root/sce_sys/icon0.png" "$dist_dir/sce_sys/"
cp "$root/sce_sys/param.json" "$dist_dir/sce_sys/"
if [[ -f "$root/sce_sys/snd0.at9" ]]; then
    cp "$root/sce_sys/snd0.at9" "$dist_dir/sce_sys/"
fi

# Copiar Lapy JB Daemon integrado y autónomo
mkdir -p "$dist_dir/daemon" "$root/dist/lapy_jb"
if [[ -d "$root/payloads/lapy_jb" ]]; then
    cp -r "$root/payloads/lapy_jb/"* "$dist_dir/daemon/"
    cp -r "$root/payloads/lapy_jb/"* "$root/dist/lapy_jb/"
    chmod 0777 "$dist_dir/daemon/lapy_jb_daemon.elf" 2>/dev/null || true
    chmod 0777 "$root/dist/lapy_jb/lapy_jb_daemon.elf" 2>/dev/null || true
fi
chmod 0777 "$dist_dir/eboot.bin"
chmod 0777 "$dist_dir/sce_module/libc.prx" 2>/dev/null || true

# 3. Crear guía de instalación bilingüe (Inglés primero, Español después)
cat << 'EOF' > "$dist_dir/INSTRUCCIONES_PS5.txt"
=====================================================
   MacheteSVG v1.0.0 - PS5 & PS4 Savegame Manager
=====================================================

[ENGLISH GUIDE]
CONFIRMED COMPATIBILITY:
- Exploit: Relapse (Firmwares 7.00 - 13.60)
- Payloads / Environments: OnionHEN, etaHEN, Lapy JB Daemon
- Mounting tool: ShadowMount Plus (recommended) or native libSceSaveData
- Save Formats: Native PS5 titles and PS4 Backward Compatibility (BC)

NO EXTERNAL DAEMONS OR ELFS REQUIRED:
- MacheteSVG is completely self-contained.
- If OnionHEN or a resident daemon is active, MacheteSVG seamlessly connects to it.
- If NO daemon is active, MacheteSVG automatically launches its embedded daemon
  via the local loader on 127.0.0.1:9021 without requiring user interaction.

PACKAGE CONTENTS:
- PPSA99001/eboot.bin             (Signed native application executable)
- PPSA99001/sce_module/libc.prx   (Prospero runtime compatibility layer)
- PPSA99001/sce_sys/param.json    (Title ID: PPSA99001 package metadata)
- PPSA99001/sce_sys/icon0.png     (Official Liquid Glass Memory Card icon)
- PPSA99001/assets/               (SDF fonts, hi-res textures, UI soundbank)
- PPSA99001/daemon/               (Embedded fallback daemon ELF)

INSTALLATION INSTRUCTIONS:
METHOD 1: VIA FTP (Recommended)
1. Ensure PS5 and PC are on the same local network.
2. Enable FTP on PS5 (default port 2121 in OnionHEN / etaHEN).
3. Connect with FileZilla/WinSCP and upload the "PPSA99001" folder to:
     /data/homebrew/
   Final path must be:
     /data/homebrew/PPSA99001/eboot.bin
4. Restart your homebrew launcher (Itemzflow or main home screen).

METHOD 2: VIA USB DRIVE (exFAT)
1. Copy "PPSA99001" folder to the root of an exFAT USB drive.
2. Insert USB into PS5, open PS5 Explorer or Itemzflow, and copy to /data/homebrew/.

CONTROLLER SHORTCUTS:
- D-Pad: Navigate game list and actions
- Cross (X): Select / Perform action
- Circle (O): Back / Cancel
- Triangle: Focus / Open Google Drive Cloud sync modal
- Square: Refresh games and USB devices
- L3 (Stick Press): Toggle language (English <-> Spanish) in real-time!

-----------------------------------------------------

[GUIA EN ESPAÑOL]
COMPATIBILIDAD CONFIRMADA:
- Exploit: Relapse (Firmwares 7.00 - 13.60)
- Payloads / Entornos: OnionHEN, etaHEN, Lapy JB Daemon
- Herramienta de montaje: ShadowMount Plus o libSceSaveData nativo
- Formatos: Guardados nativos de PS5 y títulos de PS4 en retrocompatibilidad (BC)

TOTALMENTE AUTONOMO - SIN DESCARGAS ADICIONALES:
- MacheteSVG incluye auto-lanzamiento de daemon idéntico a ProsperoStore.
- Se conecta a 127.0.0.1:9021 automáticamente si no hay daemon activo.

CONTENIDO DEL PAQUETE:
- PPSA99001/eboot.bin             (Ejecutable nativo firmado)
- PPSA99001/sce_module/libc.prx   (Runtime Prospero)
- PPSA99001/sce_sys/param.json    (Metadatos Title ID: PPSA99001)
- PPSA99001/sce_sys/icon0.png     (Icono oficial)
- PPSA99001/daemon/               (Daemon integrado)

INSTALACION EN PS5:
1. Subir la carpeta "PPSA99001" a /data/homebrew/ en la consola.
2. La ruta final debe ser: /data/homebrew/PPSA99001/eboot.bin
3. Refrescar el lanzador e iniciar la aplicación.
=====================================================
EOF

echo "==> [5/5] Creando archivo ZIP final listo para transferir..."
zip_output="$root/dist/MacheteSVG_v${version}_PS5.zip"
(cd "$root/dist" && zip -r -q "$zip_output" "$title_id" "lapy_jb")

echo ""
echo "=================================================="
echo " ¡Empaquetado completado con éxito!"
echo " Paquete generado: $zip_output"
echo " Carpeta de app:   $dist_dir"
echo "=================================================="
