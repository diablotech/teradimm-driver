# Define the kmod package name here.

%define RPMVER @VERSION@

%define is_rh %(test -e /etc/redhat-release && echo 1 || echo 0)
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%if %{is_rh}
 %define rh_version %(rpm -q --queryformat='%{VERSION}' -f /etc/redhat-release | sed - -e 's/\\([0-9]*\\).*/\\1/')
%else
 %define rh_version 0
%endif

%if "%{?tools}" != "none"
 %define tools  tdctl tdcmd
%endif

# If kversion isn't defined on the rpmbuild line, define it here.
%{!?kversion: %define kversion none}

%if "%{suppress_dist}" == "YesPlease"
 %define dist { }
%endif

Name:    teradimm
Version: %{RPMVER}
Release: 1%{?dist}
Group:   System Environment/Kernel
License: Commercial
Summary: TeraDIMM kernel module(s) and driver tools
URL:     http://www.diablo-technologies.com/

%if %{is_rh}
BuildRequires: redhat-rpm-config
%endif
ExclusiveArch: x86_64

# Sources.
Source0:  teradimm-%{RPMVER}.tar.gz


##
## Sub-package for the kernel driver, only built if kversion is not "none"
##
%if "%{kversion}" != "none"

%if %{is_rh}
BuildRequires: kernel-devel = %{kversion}
%else
BuildRequires: kernel-default-devel = %{kversion}
%endif

%package  kmod-%{kversion}
Group:   System Environment/Kernel
Provides: kmod-teradimm-{%kversion}-%{RPMVER}
Summary:  TeraDIMM Kernel module

%description kmod-%{kversion}
TeraDIMM kernel module for %{kversion}

%files kmod-%{kversion}
%defattr(-,root,root,-)
%if %{is_rh}
 %if "%{rh_version}" == "5"
/lib/modules/%{kversion}/extra/diablo/teradimm.ko
 %else
/lib/modules/%{kversion}.%{_target_cpu}/extra/diablo/teradimm.ko
 %endif
%else
/lib/modules/%{kversion}-default/extra/diablo/teradimm.ko
%endif
/etc/depmod.d/kmod-teradimm.conf
%config /etc/udev/rules.d/60-persistent-teradimm.rules

%post kmod-%{kversion}
%if %{is_rh}
 %if "%{rh_version}" == "5"
/sbin/depmod -a %{kversion}
 %else
/sbin/depmod -a %{kversion}.%{_target_cpu}
 %endif
%else
/sbin/depmod -a %{kversion}-default
%endif

%postun kmod-%{kversion}
%if %{is_rh}
 %if "%{rh_version}" == "5"
/sbin/depmod -a %{kversion}
find /lib/modules -lname /lib/modules/%{kversion}/extra/diablo/teradimm.ko -delete
 %else
/sbin/depmod -a %{kversion}.%{_target_cpu}
find /lib/modules -lname /lib/modules/%{kversion}.%{_target_cpu}/extra/diablo/teradimm.ko -delete
 %endif
%else
/sbin/depmod -a %{kversion}-default
%endif

%endif

## 
## Sub-package for "tools", built if it's not "none"
##
%if "%{tools}" != "none"
%package tools
Group:   Applications/System
Summary:  TeraDIMM kernel module driver tools

%description tools
TeraDIMM kernel module driver tools

%files tools
%defattr(-,root,root,-)
/usr/sbin/*

%endif

# Disable the building of the debug package(s).
%define debug_package %{nil}

# Define the filter.
%define __find_requires sh %{_builddir}/%{buildsubdir}/filter-requires.sh

%description

%prep
%setup -q
echo "override teradimm * weak-updates/teradimm" > kmod-teradimm.conf

echo DEST_BIN=%{buildroot}/usr/sbin > Makefile.conf

%if "%{tools}" != "none"
  echo "Preparing tools %{tools}"
%endif

%if "%{kversion}" != "none" 
 %if %{is_rh}
  %if "%{rh_version}" == "5"
  echo "Preparing driver:  kversion %{kversion}"
  %else
  echo "Preparing driver:  kversion %{kversion}.%{_target_cpu}"
  %endif
 %else
  echo "Preparing driver:  kversion %{kversion}"
 %endif
%endif

%build

%if "%{kversion}" != "none"
  echo "Building kernel driver: kversion %{kversion}"
 %if %{is_rh}
  %if "%{rh_version}" == "5"
  make %{?_smp_mflags} KVERSION=%{kversion} -C linux/driver
  %else
  make %{?_smp_mflags} KVERSION=%{kversion}.%{_target_cpu} -C linux/driver
  %endif
 %else
  make %{?_smp_mflags} -C linux/driver KDIR=/lib/modules/%{kversion}-default/build
 %endif
%endif


%if "%{tools}" != "none"
  for tool in %{tools}
  do
    make %{?_smp_mflags} KVERSION=%{kversion}.%{_target_cpu} -C tools/$tool
  done
%endif

%install
%{__rm} -rf %{buildroot}
%if "%{kversion}" != "none"
  export INSTALL_MOD_PATH=%{buildroot}
  export INSTALL_MOD_DIR=extra/diablo
%if %{is_rh}
 %if "%{rh_version}" == "5"
  KSRC=%{_usrsrc}/kernels/%{kversion}-%{_target_cpu}
 %else
  KSRC=%{_usrsrc}/kernels/%{kversion}.%{_target_cpu}
 %endif
%else
  KSRC=%{_usrsrc}/linux-%{kversion}
%endif
  %{__make} -C "${KSRC}" modules_install M=$PWD/linux/driver
  ####make %{?_smp_mflags} KVERSION=%{kversion}.%{_target_cpu} -C linux/driver install-kmod
%if "%{rh_version}" == "7"
  find %{buildroot}/lib/modules -iname 'modules.*' -exec rm {} \;
%endif
  %{__install} -d %{buildroot}%{_sysconfdir}/depmod.d/
  %{__install} kmod-teradimm.conf %{buildroot}%{_sysconfdir}/depmod.d/
  # Set the module(s) to be executable, so that they will be stripped when packaged.
  find %{buildroot} -type f -name \*.ko -exec %{__chmod} u+x \{\} \;

  # Install the udev rule.
  %{__install} -d %{buildroot}%{_sysconfdir}/udev/rules.d
  %{__install} linux/udev/60-persistent-teradimm.rules %{buildroot}%{_sysconfdir}/udev/rules.d/
%endif

%if "%{tools}" != "none"
  %{__install} -d %{buildroot}/usr/sbin
  for tool in %{tools}
  do
    make %{?_smp_mflags} KVERSION=%{kversion}.%{_target_cpu} -C tools/$tool install
  done
%endif

%clean
%{__rm} -rf %{buildroot}

%changelog
* Mon Dec 2 2013 Liam R. Howlett <lhowlett@diablo-technologies.com> - 0.3
- Added BuildRoot define for RHEL5 build.
- Moved udev rule to driver package & changed file type to %config directive,
this allows multiple drivers to be installed.
* Sun Oct 6 2013 Marc Tremblay <mtremblay@diablo-technologies.com> - 0.2
- Modify spec to be useable on all supported RHEL and SLES releases
* Mon Sep 23 2013 Aidan Van Dyk <avandyk@diablo-technologies.com> - 0.1
- Build build tools and kmod packages from same spec
* Wed Apr 4 2012 Bart Trojanowski <bart@diablo-technologies.com> - 0.0
- Initial el5 build of the kmod package.
