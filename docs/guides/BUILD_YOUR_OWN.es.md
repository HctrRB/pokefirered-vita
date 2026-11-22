# Compila tu propia VPK

Type: guide · Audience: players · Status: current
Verified against: `pokefirered-vita` source snapshot

> **Versión experimental en desarrollo activo.** Por favor, reporta los errores que encuentres
> (ver el final de esta guía).

Este repositorio no incluye los gráficos, sonidos ni datos binarios del juego, y no se distribuyen
VPK compiladas. Tú haces tu propia VPK a partir de tu copia del juego:

1. Necesitas un volcado de **tu cartucho de Pokémon Rojo Fuego / FireRed** (`.gba`, 16 MB). El
   script lee su cabecera en local solo para comprobar que es Pokémon FireRed; no se copia ni se
   sube nada de tu ROM.
2. Después descarga los archivos del juego de la decompilación
   [pret/pokefirered](https://github.com/pret/pokefirered) y comprueba cada uno contra
   `tools/assets/manifest.txt`.
3. Compila las herramientas, convierte los datos, genera `assets/ui2/ui_atlas.bin` y compila
   `FireRedVita.vpk` para instalarla en tu PS Vita.

No compartas la VPK ni los archivos descargados: contienen datos del juego de Nintendo.

Versión en inglés: [BUILD_YOUR_OWN.md](BUILD_YOUR_OWN.md).

## Qué necesitas

- Un PC con Windows 10 u 11, conexión a internet y unos 5 GB libres.
- Tu volcado de Pokémon FireRed (`.gba`, 16 MB).
- Una PS Vita con HENkaku o Ensō y VitaShell.

Todo lo demás (MSYS2, VitaSDK, Python, Git) lo instala el script.

## 1. Descarga el repositorio

Clónalo con Git:

```powershell
git clone https://github.com/HctrRB/pokefirered-vita.git C:\FireRed\pokefirered-vita
```

o, sin Git, usa **Code > Download ZIP** en GitHub y descomprímelo.

Ponlo en una carpeta cuya ruta **no tenga espacios ni tildes**, por ejemplo
`C:\FireRed\pokefirered-vita`. Las herramientas de compilación no los soportan (el script lo
comprueba y se para), así que `C:\Users\Tu Nombre\Downloads\...` no sirve. Evita las carpetas que
sincroniza OneDrive (a menudo *Documentos* y *Escritorio*): la compilación escribe miles de archivos
y la sincronización puede bloquearlos.

## 2. Compila

Haz doble clic en **`build_all.bat`**, en la carpeta del repositorio. Una ventana te pide tu `.gba`,
y la consola se queda abierta al final con el resultado. (Si Windows avisa de que el archivo viene
de internet, elige *Más información > Ejecutar de todas formas*.)

O, desde PowerShell en la carpeta del repositorio:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\build_all.ps1 -Rom "C:\path\to\firered.gba"
```

Sirve cualquier ruta de la ROM, con espacios incluidos; sin `-Rom`, la ventana te la pide.

El script:

1. instala MSYS2 en `C:\msys64` si no está, y los paquetes que necesita la compilación;
2. instala VitaSDK en MSYS2 (`/usr/local/vitasdk`) y las librerías que usa el juego (vitaGL,
   SDL2) si no están;
3. comprueba tu volcado y descarga los archivos del juego listados en `tools/assets/manifest.txt`;
4. compila las herramientas, convierte los archivos del juego, genera `assets/ui2/ui_atlas.bin` y
   compila la VPK.

La primera vez descarga 1–2 GB y puede tardar 20–40 minutos; las siguientes, unos minutos. El
resultado es **`FireRedVita.vpk`** en la carpeta del repositorio. Los pasos ya hechos se saltan, así
que puedes volver a ejecutarlo después de un fallo.

**Actualizar:** `git pull` (o vuelve a descargar el ZIP encima de la misma carpeta) y ejecuta
`build_all.bat` otra vez. La ROM solo se pide la primera vez: si una versión nueva necesita otros
archivos del juego, el script los descarga solo.

## 3. Instala

1. Copia `FireRedVita.vpk` a la Vita (por USB o FTP con VitaShell).
2. En VitaShell, pulsa X sobre la VPK e instálala (en los ajustes de HENkaku tiene que estar
   activado *Enable unsafe homebrew*).
3. El juego aparece como **FireRed Vita**. Las partidas y los ajustes se guardan en
   `ux0:data/FRVITA/`.

Si quieres tu propio arte en el LiveArea, ponlo en `vita/sce_sys_custom/` con los mismos nombres
que en `vita/sce_sys/` (PNG indexados de 8 bits); se usa en lugar de las imágenes neutras.

## Errores frecuentes

| Mensaje | Solución |
|---|---|
| `This folder's path has spaces or special characters` | Mueve la carpeta del repositorio a una ruta sencilla, como `C:\FireRed\pokefirered-vita`. |
| `Permission denied` / `Device or resource busy` al compilar | Suele ser OneDrive o un antivirus bloqueando archivos: saca la carpeta de OneDrive, o vuelve a ejecutar el script. |
| `Could not download MSYS2` / `MSYS2 did not install` | Instala MSYS2 a mano desde [msys2.org](https://www.msys2.org) en `C:\msys64` y vuelve a ejecutar el script. |
| `not a Pokémon FireRed dump` | El archivo no es una ROM de Pokémon FireRed o está dañado. Tiene que medir 16 MB. |
| `VitaSDK did not install` / `... is still missing after vdpm install` | Casi siempre es una descarga que falló: vuelve a ejecutar el script. Si no, mira [BUILD.md](BUILD.md#toolchain-requirements-windows--msys2). |
| `running scripts is disabled on this system` | Lánzalo exactamente como arriba (`powershell.exe -ExecutionPolicy Bypass -File ...`). |

## Compilar a mano

El script hace estos pasos; puedes hacerlos tú en la consola *MSYS2 MINGW64*, desde la carpeta del
repositorio (`cd /c/FireRed/pokefirered-vita`):

1. Paquetes y VitaSDK, una vez (los comandos exactos están en
   [BUILD.md](BUILD.md#toolchain-requirements-windows--msys2)).
2. Los archivos del juego, una vez:
   ```sh
   ./fetch_assets.sh /c/path/to/firered.gba
   ```
3. Las herramientas, los archivos convertidos y el atlas de la interfaz:
   ```sh
   make -f make_tools.mk -j8 CC="gcc -static" CXX="g++ -static"
   make -f vita/assets.mk -j8
   python3 tools/ui2/build_atlas.py
   ```
4. La VPK: `./build_vita.sh` -> `build-vita/pokefirered.vpk`.

## Reportar errores

Este port es una **versión experimental en desarrollo**. Si algo falla, abre un *issue* en GitHub
con la plantilla *Bug report* e incluye:

- qué estabas haciendo y qué pasó (una foto de la pantalla ayuda);
- los archivos `perf_summary.txt`, `perf_frames.csv` y cualquier `*.log` de `ux0:data/FRVITA/`;
- si se cerró el juego, el archivo `.psp2dmp` de `ux0:data/` (ver [DEBUGGING.md](DEBUGGING.md));
- el commit que compilaste (`git log -1 --oneline`);
- si falló la compilación, las últimas líneas que mostró el script.
