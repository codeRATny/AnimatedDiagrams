#!/usr/bin/env bash
set -euo pipefail

: "${CONDA_PREFIX:?Activate the build environment first}"
: "${APPDIR:?Set APPDIR to an empty AppDir path}"
: "${LINUXDEPLOY:?Set LINUXDEPLOY to the linuxdeploy AppImage}"
: "${OUTPUT:?Set OUTPUT to the desired AppImage path}"

export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$CONDA_PREFIX/bin/qmake6"
export LD_LIBRARY_PATH="$CONDA_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ARCH=x86_64

DESTDIR="$APPDIR" cmake --install "$1" --prefix /usr
"$LINUXDEPLOY" --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/animated-diagrams.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/512x512/apps/animated-diagrams.png" --plugin qt

# linuxdeploy omits offscreen and some libraries considered part of a desktop system.
# The offscreen plugin is needed by command-line export on headless machines.
cp "$CONDA_PREFIX/lib/qt6/plugins/platforms/libqoffscreen.so" "$APPDIR/usr/plugins/platforms/"
cp -L "$CONDA_PREFIX/lib/libstdc++.so.6" "$CONDA_PREFIX/lib/libgcc_s.so.1" "$APPDIR/usr/lib/"

# Copy transitive dependencies from the build prefix, including libraries excluded by
# linuxdeploy. Keep glibc and its dynamic loader supplied by the host.
for pass in {1..10}; do
    added=0
    while IFS= read -r library; do
        case "$library" in
            libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libresolv.so.*|libutil.so.*|ld-linux*) continue ;;
        esac
        if [[ ! -e "$APPDIR/usr/lib/$library" && -e "$CONDA_PREFIX/lib/$library" ]]; then
            cp -L "$CONDA_PREFIX/lib/$library" "$APPDIR/usr/lib/$library"
            added=1
        fi
    done < <(find "$APPDIR/usr" -type f -print0 | xargs -0 -n1 sh -c 'ldd "$0" 2>/dev/null || true' |
        awk '/=>/ {print $1}' | sort -u)
    (( added )) || break
done

python3 "$(dirname "$0")/check-glibc.py" "$APPDIR"
"$LINUXDEPLOY" --appdir "$APPDIR" --output appimage
python3 "$(dirname "$0")/check-glibc.py" "$APPDIR"
