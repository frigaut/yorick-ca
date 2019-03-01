%define _prefix __auto__
%define version __auto__

Summary: Channel Access for Yorick
Name: yorick-ca
Version: %{version}
Release: 1%{?dist}.gemini
URL: http://www.gemini.edu
Packager: Matthieu Bec <mbec@gemini.edu>
License: frigaut@gemini.edu
Group: Gemini
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires: yorick epics-base-devel
Requires: yorick epics-base

Source0: %{name}-%{version}.tar.gz

%define debug_package %{nil}
%define y_exe_home %(echo Y_HOME  | yorick -q | awk -F '"' '{print $2}')
%define y_exe_site %(echo Y_SITE  | yorick -q | awk -F '"' '{print $2}')

%description
EPICS channel access client wrapper for yorick.

%prep
%setup -q -n %name

%build
gmake -f yorick-ca.make1st
gmake

%install
mkdir -p  $RPM_BUILD_ROOT/%y_exe_home/lib $RPM_BUILD_ROOT/%y_exe_site/i-start $RPM_BUILD_ROOT/%y_exe_site/i0
cp ca.so $RPM_BUILD_ROOT/%y_exe_home/lib
cp ca_start.i $RPM_BUILD_ROOT/%y_exe_site/i-start
cp ca.i $RPM_BUILD_ROOT/%y_exe_site/i0

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%y_exe_home/lib/ca.so
%y_exe_site/i-start/ca_start.i
%y_exe_site/i0/ca.i

%changelog
* Fri Mar 20 2009 Matthieu Bec <mbec@gemini.edu> 1.0-1
- repackaged version 1.0 as yorick-ca
* Mon Oct 27 2008 Matthieu Bec <mbec@gemini.edu> 0.1-1
- first spec file compatible with gemini-buildrpm
