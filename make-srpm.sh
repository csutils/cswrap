#/bin/bash

# Copyright (C) 2013 Red Hat, Inc.
#
# This file is part of abscc.
#
# abscc is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.
#
# abscc is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with abscc.  If not, see <http://www.gnu.org/licenses/>.

SELF="$0"

PKG="abscc"

die(){
    echo "$SELF: error: $1" >&2
    exit 1
}

DST="`readlink -f "$PWD"`"

REPO="`git rev-parse --show-toplevel`" \
    || die "not in a git repo"

printf "%s: considering release of %s using %s...\n" \
    "$SELF" "$PKG" "$REPO"

branch="`git status | head -1 | sed 's/^#.* //'`" \
    || die "unable to read git branch"

test xmaster = "x$branch" \
    || die "not in master branch"

test -z "`git diff HEAD`" \
    || die "HEAD dirty"

test -z "`git diff origin/master`" \
    || die "not synced with origin/master"

VER="0.`git log --pretty="%cd_%h" --date=short -1 | tr -d -`" \
    || die "git log failed"

NV="${PKG}-$VER"
SRC="${PKG}.tar.xz"

TMP="`mktemp -d`"
trap "echo --- $SELF: removing $TMP... 2>&1; rm -rf '$TMP'" EXIT
test -d "$TMP" || die "mktemp failed"
SPEC="$TMP/$PKG.spec"
cat > "$SPEC" << EOF
Name:       $PKG
Version:    $VER
Release:    1%{?dist}
Summary:    GCC wrapper canonicalizing file names in warning messages

Group:      Development/Tools
License:    GPLv3+
URL:        https://engineering.redhat.com/trac/CoverityScan
Source0:    http://git.engineering.redhat.com/?p=users/kdudka/coverity-scan.git;a=blob_plain;f=abscc/abscc.c
Source1:    http://git.engineering.redhat.com/?p=users/kdudka/coverity-scan.git;a=blob_plain;f=abscc/Makefile

BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%description
Experimentally used by cov-mockbuild, not yet fully documented.

%prep
rm -rf %{name}-%{version}
install -m0755 -d %{name}-%{version}
cd %{name}-%{version}
install -m0644 %{SOURCE0} %{SOURCE1} .

%build
cd %{name}-%{version}
make %{?_smp_mflags}

%check
cd %{name}-%{version}
PATH=\$RPM_BUILD_ROOT%{_libdir}/abscc:\$PATH
export PATH
make clean
make %{?_smp_mflags} CFLAGS="-ansi -pedantic" 2>&1 | grep "\$PWD" > /dev/null

%clean
rm -rf "\$RPM_BUILD_ROOT"

%install
cd %{name}-%{version}
rm -rf "\$RPM_BUILD_ROOT"

install -m0755 -d \\
    "\$RPM_BUILD_ROOT%{_bindir}"                \\
    "\$RPM_BUILD_ROOT%{_libdir}"                \\
    "\$RPM_BUILD_ROOT%{_libdir}/abscc"

install -m0755 %{name} "\$RPM_BUILD_ROOT%{_bindir}"

for i in c++ cc g++ gcc \\
    %{_arch}-redhat-linux-c++ \\
    %{_arch}-redhat-linux-g++ \\
    %{_arch}-redhat-linux-gcc
do
    ln -s ../../bin/abscc "\$RPM_BUILD_ROOT%{_libdir}/abscc/\$i"
done

# force generating the %{name}-debuginfo package
%{debug_package}

%files
%defattr(-,root,root,-)
%{_bindir}/abscc
%{_libdir}/abscc
EOF

rpmbuild -bs "$SPEC"                            \
    --define "_sourcedir ."                     \
    --define "_specdir ."                       \
    --define "_srcrpmdir $DST"                  \
    --define "_source_filedigest_algorithm md5" \
    --define "_binary_filedigest_algorithm md5"
