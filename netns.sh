#!/bin/bash
# Copyright (c) 2018 Austin Adams
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# This script is a wrapper around `ip netns add/delete' that allows
# network namespaces to have custom configuration files in /etc/, such
# as /etc/resolv.conf. It imitates how iproute2 persists network
# namespaces by bind-mounting /proc/self/ns/net to files in
# /var/run/netns, except with mount namespaces in /var/run/mountns
# instead, and then mounts an overlayfs merging /etc/ with /etc/netns/X
# to /etc/. nsdo will setns() to /var/run/mountns/X if it exists, making
# the program it runs use the netns-specific /etc/ changes.

set -e

usage() {
    printf 'usage: netns.sh <add|delete> <nsname>\n' >&2
    exit 1
}

[[ $# -ne 2 ]] && {
    usage
}

action=$1
nsname=$2
mountns_dir=/var/run/mountns
mountns_path=$mountns_dir/$nsname
etc_netns_path=/etc/netns/$nsname

add() {
    ip netns add "$nsname"

    if [[ -e $etc_netns_path ]]; then
        mkdir -p "$mountns_dir" "$etc_netns_path"

        # systemd makes mountpoints shared by default, but they need to
        # be private to bind-mount /proc/self/ns/mnt to /run/mountns/X.
        # See:
        # https://github.com/karelzak/util-linux/issues/289#issuecomment-188224471
        if ! mountpoint "$mountns_dir" >/dev/null; then
            mount --bind "$mountns_dir" "$mountns_dir"
            mount --make-private "$mountns_dir"
        fi

        touch "$mountns_path"
        unshare --mount="$mountns_path" \
            mount -t overlay overlay -o lowerdir="$etc_netns_path:/etc/" /etc/
    fi
}

delete() {
    ip netns delete "$nsname"

    if [[ -e $mountns_path ]]; then
        nsenter --mount="$mountns_path" umount /etc/
        umount "$mountns_path"
        rm "$mountns_path"
    fi
}

case $action in
    add|delete) $action ;;
    *) usage ;;
esac
