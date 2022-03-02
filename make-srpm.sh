#!/bin/bash

# Copyright (C) 2013-2022 Red Hat, Inc.
#
# This file is part of cswrap.
#
# cswrap is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.
#
# cswrap is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with cswrap.  If not, see <http://www.gnu.org/licenses/>.

SELF="$0"

PKG="cswrap"

die() {
    echo "$SELF: error: $1" >&2
    exit 1
}

match() {
    grep "$@" > /dev/null
}

DST="`readlink -f "$PWD"`"

REPO="`git rev-parse --show-toplevel`"
test -d "$REPO" || die "not in a git repo"

NV="`git describe --tags`"
echo "$NV" | match "^$PKG-" || die "release tag not found"

VER="`echo "$NV" | sed "s/^$PKG-//"`"

TIMESTAMP="`git log --pretty="%cd" --date=iso -1 \
    | tr -d ':-' | tr ' ' . | cut -d. -f 1,2`"

VER="`echo "$VER" | sed "s/-.*-/.$TIMESTAMP./"`"

BRANCH="`git rev-parse --abbrev-ref HEAD`"
test -n "$BRANCH" || die "failed to get current branch name"
test "main" = "${BRANCH}" || VER="${VER}.${BRANCH//[\/-]/_}"
test -z "`git diff HEAD`" || VER="${VER}.dirty"

NV="${PKG}-${VER}"
printf "%s: preparing a release of \033[1;32m%s\033[0m\n" "$SELF" "$NV"

SPEC="$PKG.spec"

if [[ "$1" != "--generate-spec" ]]; then
    TMP="`mktemp -d`"
    trap "rm -rf '$TMP'" EXIT
    cd "$TMP" >/dev/null || die "mktemp failed"

    # clone the repository
    git clone "$REPO" "$PKG"                || die "git clone failed"
    cd "$PKG"                               || die "git clone failed"

    make distcheck                          || die "'make distcheck' has failed"

    SRC_TAR="${NV}.tar"
    SRC="${SRC_TAR}.xz"
    git archive --prefix="$NV/" --format="tar" HEAD -- . > "${TMP}/${SRC_TAR}" \
                                            || die "failed to export sources"
    cd "$TMP" >/dev/null                    || die "mktemp failed"
    xz -c "$SRC_TAR" > "$SRC"               || die "failed to compress sources"

    SPEC="$TMP/$SPEC"
fi

cat > "$SPEC" << EOF
%define csexec_archs aarch64 x86_64

Name:       $PKG
Version:    $VER
Release:    1%{?dist}
Summary:    Generic compiler wrapper

Group:      Development/Tools
License:    GPLv3+
URL:        https://github.com/csutils/%{name}
Source0:    https://github.com/csutils/%{name}/releases/download/%{name}-%{version}/%{name}-%{version}.tar.xz

# cswrap-1.3.0+ emits internal warnings per timed out scans (used by csdiff to
# eliminate false positivies that such a scan would otherwise cause) ==> force
# new enough versions of the higher-level tools that will suppress them.
Conflicts: csbuild       < 1.7.0
Conflicts: csdiff        < 1.2.0
Conflicts: csmock-common < 1.7.0

BuildRequires: asciidoc
BuildRequires: cmake3
BuildRequires: gcc

# The test-suite runs automatically trough valgrind if valgrind is available
# on the system.  By not installing valgrind into mock's chroot, we disable
# this feature for production builds on architectures where valgrind is known
# to be less reliable, in order to avoid unnecessary build failures (see RHBZ
# #810992, #816175, and #886891).  Nevertheless developers are free to install
# valgrind manually to improve test coverage on any architecture.
%ifarch %{ix86} x86_64
BuildRequires: valgrind
%endif

# csmock copies the resulting cswrap binary into mock chroot, which may contain
# an older (e.g. RHEL-5) version of glibc, and it would not dynamically link
# against the old version of glibc if it was built against a newer one.
# Therefor we link glibc statically.
%if (0%{?fedora} >= 12 || 0%{?rhel} >= 6)
BuildRequires: glibc-static
%endif

%description
Generic compiler wrapper used by csmock to capture diagnostic messages.

# csexec is available on aarch64 and x86_64 only for now
%ifarch %{csexec_archs}
%package -n csexec
Summary: Dynamic linker wrapper
Conflicts: csexec < %{version}-%{release}

%description -n csexec
This package contains csexec - a dynamic linker wrapper.  The wrapper can
be used to run dynamic analyzers and formal verifiers on source RPM package
fully automatically.
%endif

%prep
%setup -q

%build
mkdir cswrap_build
cd cswrap_build
%cmake3 -S.. .. -B. \\
    -DPATH_TO_WRAP=\"%{_libdir}/cswrap\" \\
    -DSTATIC_LINKING=ON
make %{?_smp_mflags} VERBOSE=yes

%check
cd cswrap_build
ctest3 %{?_smp_mflags} --output-on-failure

%install
cd cswrap_build
make install DESTDIR="\$RPM_BUILD_ROOT"

install -m0755 -d "\$RPM_BUILD_ROOT%{_libdir}"{,/cswrap}
for i in c++ cc g++ gcc clang clang++ cppcheck smatch \\
    divc++ divcc diosc++ dioscc gclang++ gclang goto-gcc \\
    %{_arch}-redhat-linux-c++ \\
    %{_arch}-redhat-linux-g++ \\
    %{_arch}-redhat-linux-gcc
do
    ln -s ../../bin/cswrap "\$RPM_BUILD_ROOT%{_libdir}/cswrap/\$i"
done

%files
%{_bindir}/cswrap
%{_libdir}/cswrap
%{_mandir}/man1/%{name}.1*
%doc COPYING README

%ifarch %{csexec_archs}
%files -n csexec
%{_bindir}/csexec
%{_bindir}/csexec-loader
%{_libdir}/libcsexec-preload.so
%{_mandir}/man1/csexec.1*
%doc COPYING
%endif
EOF

if [[ "$1" != "--generate-spec" ]]; then
    rpmbuild -bs "$SPEC"                            \
        --define "_sourcedir $TMP"                  \
        --define "_specdir $TMP"                    \
        --define "_srcrpmdir $DST"
fi
