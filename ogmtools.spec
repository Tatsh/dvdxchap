Summary: Tools for Ogg media streams
Name:    ogmtools
Version: 1.5
Release: 1
Source0: ogmtools-%{version}.tar.bz2
License: GPL
Group: Sound/Tools
BuildRoot: %{_tmppath}/%{name}-buildroot
URL: http://www.bunkus.org/videotools/ogmtools/

%description
These tools allow information about (ogminfo) or extraction from (ogmdemux) or creation of (ogmmerge) or the splitting of (ogmsplit) OGG media streams. OGM is used for "OGG media streams".

%prep
rm -rf $RPM_BUILD_ROOT

%setup -q
./autogen.sh

%configure --prefix=%{_prefix}
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT%{_prefix}/{bin,man/man1}
make DESTDIR=$RPM_BUILD_ROOT install 

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%_bindir/*
%_mandir/man1/*
%doc README INSTALL ChangeLog TODO COPYING 

%changelog
* Sun Nov 07 2004 Moritz Bunkus <moritz@bunkus.org> 1.5-1
- new upstream
* Tue Aug 03 2004 Moritz Bunkus <moritz@bunkus.org> 1.4.1-1
- new upstream
* Mon Apr 14 2004 Moritz Bunkus <moritz@bunkus.org> 1.4-1
- new upstream
* Mon Apr 05 2004 Moritz Bunkus <moritz@bunkus.org> 1.3-1
- new upstream
* Thu Nov 20 2003 Moritz Bunkus <moritz@bunkus.org> 1.2-1
- new upstream
* Tue Oct 27 2003 Moritz Bunkus <moritz@bunkus.org> 1.1-1
- new upstream
* Tue May 20 2003 Moritz Bunkus <moritz@bunkus.org> 1.0.3-1
- new upstream
* Mon May 06 2003 Moritz Bunkus <moritz@bunkus.org> 1.0.2-1
- new upstream
* Tue Mar 04 2003 Moritz Bunkus <moritz@bunkus.org> 1.0.1-1
- new upstream
* Sat Mar 01 2003 Moritz Bunkus <moritz@bunkus.org> 1.0.0-1
- new upstream
* Fri Jan 03 2003 Moritz Bunkus <moritz@bunkus.org> 0.973-1
- new upstream
* Fri Dec 13 2002 Moritz Bunkus <moritz@bunkus.org> 0.970-1
- new upstream
* Fri Nov 13 2002 Moritz Bunkus <moritz@bunkus.org> 0.960-1
- new upstream
* Tue Oct 01 2002 Moritz Bunkus <moritz@bunkus.org> 0.951-1
- new upstream
* Sun Sep 22 2002 Moritz Bunkus <moritz@bunkus.org> 0.950-1
- changes to the description and version number
* Sun Sep 22 2002 Marc Lavallée <odradek@videotron.ca> 0.931-1
- initial spac file
