# Maintainer: Zeta <zeta@localhost>
# Contributor: yxzl

pkgname=ter-music-cn
pkgver() {
  cd "$srcdir/ter-music"
  git describe --long --tags | sed 's/\([^-]*-g\)/r\1/;s/-/./g' || echo "1.0.0"
}
pkgrel=1
pkgdesc="Terminal based music player"
arch=('x86_64' 'i686')
url="https://github.com/YXZL985/ter-music"
license=('GPL')
depends=('ffmpeg' 'ncurses' 'libao' 'libmad' 'libid3tag')
makedepends=('cmake' 'make' 'gcc' 'git')
source=("ter-music::git+https://github.com/YXZL985/ter-music.git#branch=master")
sha256sums=('SKIP')
# Note: Using SKIP for git source is acceptable as git provides its own integrity verification

prepare() {
  cd "$srcdir/ter-music"
  # 修复 ncurses 头文件路径问题
  find . -name "*.c" -o -name "*.h" | xargs sed -i 's|#include <ncursesw/ncurses.h>|#include <ncurses.h>|g' 2>/dev/null || true
}

build() {
  cd "$srcdir/ter-music"
  mkdir -p build
  cd build
  cmake .. \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_BUILD_TYPE=Release
  make
}

package() {
  cd "$srcdir/ter-music/build"
  install -Dm755 ter-music "$pkgdir/usr/bin/ter-music"
}
