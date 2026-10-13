#!/bin/sh
# Build the macOS app, for both Apple Silicon and Intel, with SDL2 linked in so that
# nothing else has to be installed. Run on a Mac from the top of the source tree:
#   packaging/macos/build.sh NAME "TITLE" SOURCE.c...
# It writes dist/TITLE-VERSION-mac.zip, and a test picture in build/mac/.
set -e
name=$1; title=$2; shift 2
version=$(sed -n 's/^VERSION *:= *//p' Makefile)
sdl=2.30.9
root=$PWD/build/mac
mkdir -p "$root" dist
export MACOSX_DEPLOYMENT_TARGET=11.0

if [ ! -f "$root/sdl/lib/libSDL2.a" ]; then
    curl -sL "https://github.com/libsdl-org/SDL/releases/download/release-$sdl/SDL2-$sdl.tar.gz" | tar xz -C "$root"
    cmake -S "$root/SDL2-$sdl" -B "$root/sdl-build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
          -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_C_FLAGS=-w -DCMAKE_INSTALL_PREFIX="$root/sdl" >/dev/null
    cmake --build "$root/sdl-build" --parallel >/dev/null 2>&1
    cmake --install "$root/sdl-build" >/dev/null
fi

app="$root/$title.app"
rm -rf "$app"; mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
# the pictures are built into the program, so it is compiled from the top of the tree
cc -O2 -std=gnu11 -Wall -Isrc -arch arm64 -arch x86_64 $("$root/sdl/bin/sdl2-config" --cflags) \
   -o "$app/Contents/MacOS/$name" "$@" $("$root/sdl/bin/sdl2-config" --static-libs)
sed -e "s/@NAME@/$name/g" -e "s/@TITLE@/$title/g" -e "s/@VERSION@/$version/g" packaging/macos/Info.plist.in > "$app/Contents/Info.plist"

# the icon, in the sizes macOS wants
if [ -f "packaging/$name.png" ]; then
    set="$root/$name.iconset"; rm -rf "$set"; mkdir -p "$set"
    for s in 16 32 128 256; do
        sips -z $s $s "packaging/$name.png" --out "$set/icon_${s}x${s}.png" >/dev/null
        sips -z $((s * 2)) $((s * 2)) "packaging/$name.png" --out "$set/icon_${s}x${s}@2x.png" >/dev/null
    done
    iconutil -c icns "$set" -o "$app/Contents/Resources/$name.icns"
fi

# signed "ad hoc": enough for it to run, though not enough to stop macOS asking the first time
codesign --force --deep -s - "$app"
lipo -archs "$app/Contents/MacOS/$name"

# prove that it runs here: draw a frame without a screen and keep the picture
SDL_VIDEODRIVER=dummy "$app/Contents/MacOS/$name" --snapshot "$root/test.bmp" --time 5 --size 1280x720

ditto -c -k --keepParent "$app" "dist/$(echo "$title" | tr ' ' '-')-$version-mac.zip"
ls -l dist/*-mac.zip
