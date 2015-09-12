*Note: I base this method heavily on [a great article by Sebastian
Thorarensen][1].*

To isolate VPN applications from applications running 'bare,' I use the
following method, which involves nsdo. It has the handy effect of
putting each instance of the openvpn client in its own network
namespace, allowing you to run a bunch of VPNs at the same time, each
available only to programs you choose.

First, I leverage the [Arch `openvpn` package's `openvpn@.service`][2]
systemd [service][3], which I'll paste here for posterity:

    [Unit]
    Description=OpenVPN connection to %i

    [Service]
    Type=forking
    ExecStart=/usr/bin/openvpn --cd /etc/openvpn --config /etc/openvpn/%i.conf --daemon openvpn@%i --writepid /run/openvpn@%i.pid
    PIDFile=/run/openvpn@%i.pid

    [Install]
    WantedBy=multi-user.target

(For instance, you'd start an openvpn instance configured in
`/etc/openvpn/foo.conf` with `systemctl start openvpn@foo`.)

To make OpenVPN keep the same network namespace across VPN reconnections
or daemon restarts (e.g., after a suspend), I put the following in
`/etc/systemd/system/openvpn@.service.d/netns.conf`:

    [Unit]
    Requires=netns@%i.service
    After=netns@%i.service

And then create a `netns@.service` in `/etc/systemd/system`:

    [Unit]
    Description=network namespace %I

    [Service]
    Type=oneshot
    ExecStart=/bin/ip netns add %I
    ExecStop=/bin/ip netns del %I
    RemainAfterExit=yes

By default, openvpn manually runs `ifconfig` or `ip` to set up its tun
device. Luckily for us, you can configure openvpn to run a custom script
instead. (though you have to set `script-security` >= 2. :( )

I named my script `/usr/local/bin/vpn-ns`, so here's the relevant snippet
from my [openvpn configuration file][4]:

    # ...
    # (my other configuration)
    # ...

    # script should run `ip`, not openvpn
    route-noexec
    ifconfig-noexec
    up "/usr/local/bin/vpn-ns"
    route-up "/usr/local/bin/vpn-ns"
    script-security 2

Using [Sebastian's script][1] as a basis, I hacked together the
following. Notice that it uses iproute2 to create a network namespace
with the name of the instance's configuration file (`foo` in the earlier
example).

    #!/bin/bash
    # based heavily on http://naju.se/articles/openvpn-netns

    [[ $EUID -ne 0 ]] && {
        echo "$0: this program requires root privileges. try again with 'sudo'?" >&2
        exit 1
    }

    # convert a dot-decimal mask (e.g., 255.255.255.0) to a bit-count mask
    # (like /24) for iproute2. this probably isn't the most beautiful way.
    tobitmask() {
        bits=0
        while read -rd . octet; do
            (( col = 2**7 ))
            while (( col > 0 )); do
                (( octet & col )) || break 2
                (( bits++ ))
                (( col >>= 1 ))
            done
        done <<<"$1"
        echo $bits
    }

    # guess name of network namespace from name of config file
    basename="$(basename "$config")"
    ns="${basename%.conf}"
    netmask="$(tobitmask "$route_netmask_1")"

    case $script_type in
        up)
            ip -netns "$ns" link set dev lo up
            ip link set dev $dev up netns "$ns" mtu "$tun_mtu"
            ip -netns "$ns" addr add "$ifconfig_local/$netmask" dev "$dev"
        ;;
        route-up)
            ip -netns "$ns" route add default via "$route_vpn_gateway"
        ;;
        *)
            echo "$0: unknown \$script_type: '$script_type'" >&2
            exit 2;
        ;;
    esac

Now, once you've told systemd to start `openvpn@foo`, you can run any
application you'd like under the new namespace:

    $ nsdo foo firefox

Alternatively, if you don't want to bother with nsdo:

    $ sudo ip netns foo sudo -u $USER firefox

[1]: http://naju.se/articles/openvpn-netns
[2]: https://projects.archlinux.org/svntogit/packages.git/tree/trunk/openvpn@.service?h=packages/openvpn
[3]: http://www.freedesktop.org/software/systemd/man/systemd.service.html
[4]: https://community.openvpn.net/openvpn/wiki/Openvpn23ManPage
