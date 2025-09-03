# Fedora RPM spec file for PCBox including roms
#
# To create RPM files from this spec file, run the following commands:
#  sudo dnf install rpm-build
#  mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
#
# copy this 86Box.spec file to ~/rpmbuild/SPECS and run the following commands:
#  cd ~/rpmbuild
#  sudo dnf builddep SPECS/PCBox.spec
#  rpmbuild --undefine=_disable_source_fetch -ba SPECS/PCBox.spec
#
# After a successful build, you can install the RPMs as follows:
#  sudo dnf install RPMS/$(uname -m)/86Box-3* RPMS/noarch/86Box-roms*

%global romver 4.2.1

<<<<<<< HEAD:src/unix/assets/PCBox.spec
Name:		PCBox
Version:	5.0.1
=======
Name:		86Box
Version:	5.1
>>>>>>> 83cef9c49b78ceb72fb26ff87cb6bf0235a0fc94:src/unix/assets/86Box.spec
Release:	1%{?dist}
Summary:	Classic PC emulator
License:	GPLv2+
URL:		https://pcbox-emu.xyz/

Source0:	https://github.com/PCBox/PCBox/archive/refs/tags/v%{version}.tar.gz
Source1:	https://github.com/PCBox/roms/archive/refs/tags/v%{romver}.zip

BuildRequires: cmake
BuildRequires: desktop-file-utils
BuildRequires: extra-cmake-modules
BuildRequires: fluidsynth-devel
BuildRequires: freetype-devel
BuildRequires: gcc-c++
BuildRequires: libFAudio-devel
BuildRequires: libappstream-glib
BuildRequires: libatomic
BuildRequires: libevdev-devel
BuildRequires: libslirp-devel
BuildRequires: libxkbcommon-x11-devel
BuildRequires: libXi-devel
BuildRequires: ninja-build
BuildRequires: openal-soft-devel
BuildRequires: qt5-linguist
BuildRequires: qt5-qtconfiguration-devel
BuildRequires: qt5-qtbase-private-devel
BuildRequires: qt5-qtbase-static
BuildRequires: rtmidi-devel
BuildRequires: wayland-devel
BuildRequires: SDL2-devel

Requires: hicolor-icon-theme
Requires: fluid-soundfont-gm
Requires: PCBox-roms

%description
PCBox is a hypervisor and IBM PC system emulator that specializes in
running old operating systems and software designed for IBM
PC systems and compatibles from 1981 through fairly recent
system designs based on the PCI bus.

It supports various models of PCs, graphics and sound cards, and CPUs.

%package	roms
Summary:	ROMs for use with PCBox
Version:	%{romver}
License:	Proprietary
BuildArch:	noarch

%description	roms
Collection of ROMs for use with PCBox.

%prep
%autosetup -p1 -a1

%build
%ifarch i386 x86_64
  %cmake -DRELEASE=on
%else
  %ifarch arm aarch64
    %cmake -DRELEASE=on -DNEW_DYNAREC=on
  %else
    %cmake -DRELEASE=on -DDYNAREC=off
  %endif
%endif
%cmake_build

%install
# install base package
%cmake_install

# install icons
for i in 48 64 72 96 128 192 256 512; do
  mkdir -p $RPM_BUILD_ROOT%{_datadir}/icons/hicolor/${i}x${i}/apps
  cp src/unix/assets/${i}x${i}/net.86box.86Box.png $RPM_BUILD_ROOT%{_datadir}/icons/hicolor/${i}x${i}/apps
done

# install desktop file
desktop-file-install --dir=%{buildroot}%{_datadir}/applications src/unix/assets/net.86box.86Box.desktop

# install metadata
mkdir -p %{buildroot}%{_metainfodir}
cp src/unix/assets/net.86box.86Box.metainfo.xml %{buildroot}%{_metainfodir}
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/net.86box.86Box.metainfo.xml

# install roms
pushd roms-%{romver}
  mkdir -p %{buildroot}%{_datadir}/%{name}/roms
  cp -a * %{buildroot}%{_datadir}/%{name}/roms/
popd

# files part of the main package
%files
%license COPYING
%{_bindir}/PCBox
%{_datadir}/applications/net.86box.86Box.desktop
%{_metainfodir}/net.86box.86Box.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/net.86box.86Box.png

# files part of the rom package
%files roms
%license  roms-%{romver}/LICENSE
%{_datadir}/%{name}/roms

%changelog
* Sat Aug 31 2025 Jasmine Iwanek <jriwanek[AT]gmail.com> 5.1-1
- Bump release
