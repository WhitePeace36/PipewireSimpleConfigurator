# Maintainer: Timon <timon2201@gmail.com>
#
# Builds the Arch package from the source files next to this PKGBUILD, without downloading anything.
# Run it in the repository root:
#
#   makepkg -si
#
# The finished package (pipewire-configurator-simple-<version>-<rel>-<arch>.pkg.tar.zst) is written
# to this directory. makepkg's working folders src/ and pkg/ are created here too and can be deleted.

pkgname=pipewire-configurator-simple
pkgver=2.0
pkgrel=1
pkgdesc='GUI tool for PipeWire and WirePlumber settings'
arch=('x86_64' 'aarch64')
url='https://github.com/WhitePeace36/PipewireSimpleConfigurator'
license=('GPL-2.0-only')
depends=('qt6-base' 'pipewire' 'wireplumber' 'hicolor-icon-theme' 'glibc' 'gcc-libs')
makedepends=('cmake')
optdepends=('pipewire-pulse: settings for applications that use the PulseAudio API'
            'pipewire-jack: settings for JACK applications')

build() {
  cmake -B build -S "$startdir" \
    -DCMAKE_BUILD_TYPE=None \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -Wno-dev
  cmake --build build
}

check() {
  QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
}

package() {
  DESTDIR="$pkgdir" cmake --install build
}
