# typed: false
# frozen_string_literal: true

class AmigaGcc < Formula
  desc "GNU C/C++ cross-compiler toolchain for Amiga (m68k-amigaos)"
  homepage "https://franke.ms/git/bebbo/amiga-gcc"
  license "GPL-2.0-or-later"

  url "https://franke.ms/git/bebbo/amiga-gcc.git",
    branch:   "master",
    revision: "b47737b389375791d4f1303829d16a2de92fc319"
  version "2025.07.13"

  head "https://franke.ms/git/bebbo/amiga-gcc.git", branch: "master"

  depends_on "autoconf"  => :build
  depends_on "automake"  => :build
  depends_on "bash"      => :build
  depends_on "bison"     => :build
  depends_on "flex"      => :build
  depends_on "gcc@12"    => :build
  depends_on "gettext"   => :build
  depends_on "gnu-sed"   => :build
  depends_on "lhasa"     => :build
  depends_on "make"      => :build
  depends_on "texinfo"   => :build
  depends_on "wget"      => :build
  depends_on "gmp"
  depends_on "libmpc"
  depends_on :macos
  depends_on "mpfr"

  env :std

  def install
    hb = HOMEBREW_PREFIX

    # The amiga-gcc build is extremely sensitive to environment pollution.
    # Homebrew injects C_INCLUDE_PATH, LIBRARY_PATH, SDKROOT etc. that
    # cause the m68k cross-compiler to find macOS SDK headers.
    # The ONLY reliable fix: run the build via a shell script that
    # explicitly unsets all problematic variables.
    (buildpath/"brew-build.sh").write <<~BASH
      #!/usr/bin/env bash
      set -e

      # Clean environment: unset everything that could leak macOS paths
      # into the m68k cross-compiler
      unset SDKROOT MACOSX_DEPLOYMENT_TARGET
      unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH
      unset LIBRARY_PATH
      unset CFLAGS CXXFLAGS LDFLAGS CPPFLAGS
      unset HOMEBREW_SDKROOT HOMEBREW_ISYSTEM_PATHS HOMEBREW_LIBRARY_PATHS
      unset HOMEBREW_INCLUDE_PATHS HOMEBREW_DEPENDENCIES
      unset CMAKE_PREFIX_PATH CMAKE_INCLUDE_PATH CMAKE_LIBRARY_PATH
      unset PKG_CONFIG_PATH PKG_CONFIG_LIBDIR

      export PATH="#{hb}/opt/bison/bin:#{hb}/opt/texinfo/bin:#{hb}/opt/gnu-sed/libexec/gnubin:#{hb}/opt/gnu-sed/bin:#{hb}/opt/flex/bin:#{hb}/opt/bash/bin:#{hb}/opt/gcc@12/bin:#{hb}/opt/gettext/bin:#{hb}/opt/make/bin:#{hb}/opt/wget/bin:#{hb}/opt/lhasa/bin:#{hb}/opt/autoconf/bin:#{hb}/opt/automake/bin:#{hb}/bin:#{hb}/sbin:/usr/bin:/bin:/usr/sbin:/sbin"

      cd "#{buildpath}"

      gmake update SHELL=#{hb}/opt/bash/bin/bash SED=gsed

      # Patch libnix: rsync --exclude prevents libstubs.a from being
      # copied to PREFIX, then mv fails. Copy from build dir instead.
      sed -i.bak 's|mv \\$(PREFIX)/\\$(target)/libnix/lib/libstubs.a \\$(PREFIX)/\\$(target)/lib/|cp lib/libstubs.a \\$(PREFIX)/\\$(target)/lib/|' projects/libnix/Makefile.gcc6

      CC=gcc-12 CXX=g++-12 gmake all \\
        PREFIX=#{prefix} NDK=3.9 \\
        SHELL=#{hb}/opt/bash/bin/bash SED=gsed \\
        -j#{ENV.make_jobs}
    BASH
    chmod 0755, buildpath/"brew-build.sh"

    # Run via bash (not env -i, which strips too much)
    system "/usr/bin/env", "bash", "#{buildpath}/brew-build.sh"
  end

  def caveats
    <<~EOS
      The Amiga m68k cross-compiler toolchain is now installed.

      Tools: m68k-amigaos-gcc, g++, as, ld, ar, objdump, strip, etc.
      Also:  vasm, vlink, fd2sfd, fd2pragma, ira, sfdc

      CPU targets: -m68000 (default) through -m68060
      C runtimes:  libnix (default), -mcrt=nix13 (KS 1.3), -mcrt=clib2

      Note: vasm and vlink are free for non-commercial use only.

      Example:
        m68k-amigaos-gcc -noixemul -m68020 -O2 -o hello hello.c
    EOS
  end

  test do
    (testpath/"hello.c").write <<~C
      #include <stdio.h>
      int main(void) { printf("Hello, Amiga!\\n"); return 0; }
    C
    system bin/"m68k-amigaos-gcc", "-noixemul", "-o", "hello", "hello.c"
    assert_path_exists testpath/"hello"
    magic = (testpath/"hello").binread(4).unpack1("N")
    assert_equal 0x3F3, magic
  end
end
