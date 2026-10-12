Name:           teglon
Version:        %{version}
Release:        1%{?dist}
Summary:        Screensavers from "Anathem": the Teglon and the Lazy Peregrin
License:        MIT
Source0:        teglon-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Recommends:     swayidle

%description
The Teglon is the tiling problem in Neal Stephenson's novel Anathem: pave a
regular decagon with tiles of seven shapes so that the grooves on the tiles
join into one unbroken curve from one vertex to the opposite vertex, crossing
every tile.

teglon is a screensaver that solves it again and again, laying each solution
over the last. lazy-peregrin is a second screensaver for the Lazy Peregrin
problem, Arbre's travelling salesman. teglon-solve is the command line solver. teglon-screensaver
switches the screensaver on and off for your desktop session.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix}
rm -rf %{buildroot}%{_datadir}/doc/teglon

%files
%license LICENSE
%doc README.md
%{_bindir}/teglon
%{_bindir}/lazy-peregrin
%{_bindir}/teglon-solve
%{_bindir}/teglon-screensaver
%{_datadir}/applications/teglon.desktop
%{_datadir}/applications/lazy-peregrin.desktop
%{_datadir}/icons/hicolor/scalable/apps/teglon.svg
