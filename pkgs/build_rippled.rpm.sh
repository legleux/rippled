#!/usr/bin/env bash

dnf -y install rpm-build rpmdevtools rpmlint gnupg2
rpmdev-setuptree


mkdir -p ~/tmp/rippled-3.0.0 && cd ~/tmp/rippled-3.0.0
cat > hello <<'SH'
#!/usr/bin/env bash
echo "hello, from rippled!"
SH
chmod +x hello

printf 'MIT\n' > LICENSE
printf '# Hello RPM\n' > README.md

cd ..
tar -czf ~/rpmbuild/SOURCES/rippled-3.0.0.tar.gz rippled-3.0.0

cat > ~/rpmbuild/SPECS/rippled.spec <<'SPEC'
%global debug_package %{nil}

Name:           rippled
Version:        3.0.0
Release:        1%{?dist}
Summary:        Minimal hello world script

License:        MIT
URL:            https://example.invalid/hello
Source0:        %{name}-%{version}.tar.gz
BuildArch:      %_target_cpu
%description
Minimal hello world script.

%prep
%autosetup -n %{name}-%{version}

%build
# no build

%install
install -Dm0755 hello %{buildroot}%{_bindir}/hello

%files
%license LICENSE
%doc README.md
%{_bindir}/hello

%changelog
* Thu Oct 02 2025 You <you@example.com> - 3.0.0-1
- Initial package
SPEC

rpmbuild -ba ~/rpmbuild/SPECS/rippled.spec


## configure signing
# cat >> ~/.rpmmacros <<'MAC'
# %_signature gpg
# %_gpg_path ~/.gnupg
# %_gpg_name You <you@example.com>
# %_gpgbin /usr/bin/gpg
# MAC

# # Add signature
# rpmsign --addsign ~/rpmbuild/RPMS/noarch/rippled-3.0.0-1.el9.noarch.rpm

# # Verify signature
# rpm -Kv ~/rpmbuild/RPMS/noarch/rippled-3.0.0-1.el9.noarch.rpm
