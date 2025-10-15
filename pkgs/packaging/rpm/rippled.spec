%global pkg_name           %{getenv:repo_name}
%global branch             %{getenv:branch}
%global commit             %{getenv:commit}
%global shortcommit        %{getenv:shortcommit}
%global date               %{getenv:commit_date}
%global conan_remote_name  %{getenv:conan_remote_name}
%global conan_remote_url   %{getenv:conan_remote_url}
%global shared_files       %{getenv:shared_files}
%global pkg_files          %{getenv:pkg_files}
%global build_type         %{getenv:BUILD_TYPE}

%global _prefix           /opt/ripple
%global srcdir            %{_builddir}/rippled
%global blddir            %{srcdir}/bld.rippled

%global xrpl_version     %{getenv:xrpl_version}
%global ver_base         %(v=%{xrpl_version}; echo ${v%%-*})
%global _has_dash        %(v=%{xrpl_version}; [ "${v#*-}" != "$v" ] && echo 1 || echo 0)
%if 0%{?_has_dash}
  %global ver_suffix     %(v=%{xrpl_version}; printf %s "${v#*-}")
%endif

Name:           %{pkg_name}
Version:        %{ver_base}
Release:        %{?ver_suffix:0.%{ver_suffix}}%{!?ver_suffix:1}%{?dist}
Summary:        %{name} XRPL daemon

License:        ISC
URL:            https://github.com/XRPLF/rippled
Source0:        rippled.tar.gz
Patch0:         rippled.patch
%{warn:name=%{name}}
%{warn:version=%{version}}
%{warn:ver_base=%{ver_base}}
%{warn:ver_suffix=%{ver_suffix}}
%{warn:release=%{release}}
%{warn:FullReleaseVersion=%{name}-%{version}-%{release}.%{_arch}.rpm}

%description
%{name} with p2p server for the XRP Ledger.

%prep
%autosetup -p1 -n %{name}

# TODO: Remove when version set with CMake.
if [ %{branch} == 'develop' ]; then
  sed --in-place "s/versionString = \"\([^\"]*\)\"/versionString = \"\1+%{ver_input}\"/" src/libxrpl/protocol/BuildInfo.cpp
fi

%build
conan remote add --index 0 %{conan_remote_name} %{conan_remote_url} --force
conan config install conan/profiles/default --target-folder $(conan config home)/profiles/
echo "tools.build:jobs={{ os.cpu_count() - 2 }}" >> ${CONAN_HOME}/global.conf
echo "core.download:parallel={{ os.cpu_count() }}" >> ${CONAN_HOME}/global.conf

conan install %{srcdir} \
  --settings:all build_type=%{build_type} \
  --output-folder %{srcdir}/conan_deps \
  --options:host "&:xrpld=True" \
  --options:host "&:tests=True" \
  --build=missing

cmake \
  -S %{srcdir} \
  -B %{blddir} \
  -Dxrpld=ON \
  -Dvalidator_keys=ON \
  -Dtests=ON \
  -DCMAKE_BUILD_TYPE:STRING=%{build_type} \
  -DCMAKE_INSTALL_PREFIX:PATH=%{_prefix} \
  -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
  -DCMAKE_TOOLCHAIN_FILE:FILEPATH=%{srcdir}/conan_deps/build/generators/conan_toolchain.cmake

cmake \
  --build %{blddir} \
  --parallel %{_smp_build_ncpus} \
  --target rippled \
  --target validator-keys

%install
rm -rf %{buildroot}
DESTDIR=%{buildroot} cmake --install %{blddir}

install -Dm0755 %{shared_files}/update-rippled.sh %{buildroot}%{_bindir}/update-rippled.sh
ln -s rippled %{buildroot}%{_bindir}/xrpld
ln -s update-rippled.sh %{buildroot}%{_bindir}/update-xrpld.sh

# configs
install -Dm0644 %{srcdir}/cfg/rippled-example.cfg    %{buildroot}%{_prefix}/etc/rippled.cfg
install -Dm0644 %{srcdir}/cfg/validators-example.txt %{buildroot}%{_prefix}/etc/validators.txt
mkdir -p %{buildroot}%{_sysconfdir}/opt/ripple

#/etc points to /opt
ln -s ../../../opt/ripple/rippled.cfg        %{buildroot}%{_sysconfdir}/opt/ripple/xrpld.cfg
ln -s ../../../opt/ripple/etc/rippled.cfg    %{buildroot}%{_sysconfdir}/opt/ripple/rippled.cfg
ln -s ../../../opt/ripple/etc/validators.txt %{buildroot}%{_sysconfdir}/opt/ripple/validators.txt

# systemd/sysusers/tmpfiles
install -Dm0644 %{shared_files}/rippled.service  %{buildroot}%{_unitdir}/rippled.service
install -Dm0644 %{pkg_files}/rippled.sysusers    %{buildroot}%{_sysusersdir}/rippled.conf
install -Dm0644 %{pkg_files}/rippled.tmpfiles    %{buildroot}%{_tmpfilesdir}/rippled.conf

%files
%license LICENSE*
%doc README*

# Files/dirs the pkgs owns
%dir %{_prefix}
%dir %{_prefix}/bin
%dir %{_prefix}/etc
%if 0
  %dir %{_sysconfdir}/opt # Add this if rpmlint cries.
%endif
%dir %{_sysconfdir}/opt/ripple

# Binaries and symlinks under our (non-standard) _prefix (/opt/ripple)
%{_bindir}/rippled
%{_bindir}/xrpld
%{_bindir}/update-rippled.sh
%{_bindir}/update-xrpld.sh
%{_bindir}/validator-keys

# We won't ship these but we'll create them.
%ghost /usr/local/bin/rippled
%ghost /usr/local/bin/xrpld

%config(noreplace) %{_prefix}/etc/rippled.cfg
%config(noreplace) %{_prefix}/etc/validators.txt

%config(noreplace) %{_sysconfdir}/opt/ripple/rippled.cfg
%config(noreplace) %{_sysconfdir}/opt/ripple/xrpld.cfg
%config(noreplace) %{_sysconfdir}/opt/ripple/validators.txt

# systemd and service user creation
%{_unitdir}/rippled.service
%{_sysusersdir}/rippled.conf
%{_tmpfilesdir}/rippled.conf

# Let tmpfiles create the db and log dirs
%ghost %dir /var/opt/ripple
%ghost %dir /var/opt/ripple/lib
%ghost %dir /var/opt/ripple/log

# TODO: Fix the CMake install() calls so we don't need to exclude these.
%exclude %{_prefix}/include/*
%exclude %{_prefix}/lib/*
%exclude %{_prefix}/lib/pkgconfig/*
%exclude /usr/lib/debug/**

%post
# Add a link to $PATH /usr/local/bin/rippled  %{_bindir}/rippled (also non-standard)
mkdir -p /usr/local/bin
for i in rippled xrpld
do
  if [ ! -e /usr/local/bin/${i} ]; then
    ln -s %{_bindir}/${i} /usr/local/bin/${i}
  elif [ -L /usr/local/bin/${i} ] && \
       [ "$(readlink -f /usr/local/bin/${i})" != "%{_bindir}/${i}" ]; then
    ln -sfn %{_bindir}/${i} /usr/local/bin/${i}
  fi
done

%preun
# remove the link only if it points to us (on erase, $1 == 0)
for i in rippled xrpld
do
  if [ "$1" -eq 0 ] && [ -L /usr/local/bin/${i} ] && \
     [ "$(readlink -f /usr/local/bin/${i})" = "%{_bindir}/${i}" ]; then
    rm -f /usr/local/bin/${i}
  fi
done
