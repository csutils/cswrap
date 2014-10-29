#/bin/bash

# Copyright (C) 2013-2014 Red Hat, Inc.
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
test master = "${BRANCH}" || VER="${VER}.${BRANCH}"
test -z "`git diff HEAD`" || VER="${VER}.dirty"

NV="${PKG}-${VER}"
printf "%s: preparing a release of \033[1;32m%s\033[0m\n" "$SELF" "$NV"

TMP="`mktemp -d`"
trap "echo --- $SELF: removing $TMP... 2>&1; rm -rf '$TMP'" EXIT
cd "$TMP" >/dev/null || die "mktemp failed"

# clone the repository
git clone "$REPO" "$PKG"                || die "git clone failed"
cd "$PKG"                               || die "git clone failed"

make -j9 distcheck CTEST='ctest -j9'    || die "'make distcheck' has failed"

SRC_TAR="${NV}.tar"
SRC="${SRC_TAR}.xz"
git archive --prefix="$NV/" --format="tar" HEAD -- . > "${TMP}/${SRC_TAR}" \
                                        || die "failed to export sources"
cd "$TMP" >/dev/null                    || die "mktemp failed"
xz -c "$SRC_TAR" > "$SRC"               || die "failed to compress sources"

SPEC="$TMP/$PKG.spec"
cat > "$SPEC" << EOF
Name:       $PKG
Version:    $VER
Release:    1%{?dist}
Summary:    Generic compiler wrapper

Group:      Development/Tools
License:    GPLv3+
URL:        https://git.fedorahosted.org/cgit/cswrap.git
Source0:    https://git.fedorahosted.org/cgit/cswrap.git/snapshot/$SRC

BuildRequires: asciidoc
BuildRequires: cmake

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

%prep
%setup -q

%build
mkdir cswrap_build
cd cswrap_build
export CFLAGS="\$RPM_OPT_FLAGS"' -DPATH_TO_WRAP=\\"%{_libdir}/cswrap\\"'
export LDFLAGS="\$RPM_OPT_FLAGS -static -pthread"
%cmake ..
make %{?_smp_mflags} VERBOSE=yes

%check
cd cswrap_build
ctest %{?_smp_mflags} --output-on-failure

%install
install -m0755 -d \\
    "\$RPM_BUILD_ROOT%{_bindir}"                \\
    "\$RPM_BUILD_ROOT%{_libdir}"                \\
    "\$RPM_BUILD_ROOT%{_libdir}/cswrap"         \\
    "\$RPM_BUILD_ROOT%{_mandir}/man1"

cd cswrap_build
make install DESTDIR="\$RPM_BUILD_ROOT"

for i in c++ cc g++ gcc clang clang++ cppcheck \\
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
EOF

rpmbuild -bs "$SPEC"                            \
    --define "_sourcedir $TMP"                  \
    --define "_specdir $TMP"                    \
    --define "_srcrpmdir $DST"
