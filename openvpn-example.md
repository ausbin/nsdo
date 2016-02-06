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

And then created a `netns@.service` in `/etc/systemd/system`:

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
following. Notice that it guesses the name of the network namespace
based on the name of the instance's configuration file (e.g.,
`/etc/openvpn/foo.conf`→`foo`).

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

addendum: configuring veth
--------------------------
*Note: if you're curious about veth, [Scott Lowe's handy blog post][5],
where I found the commands below, serves as a good introduction.*

Suppose that I want to use nsdo+openvpn as described above to tunnel an
application that also provides a server (for RPC, for instance). That
is, I run an application that binds to a port *in* a namespace, but I
want to connect to it outside of that namespace.

With the setup I've described up to this point, I simply cannot do this.
Certainly, network namespaces separate running programs from one another
-- an application can't cross the line willy-nilly. For instance, I
could not use netcat to listen on a port in one namespace and then
connect to it from another:

    $ nsdo foo nc -l -p 5050 <<<"hi!" &
    $ nc -v localhost 5050 <<<"hello"
    localhost [127.0.0.1] 5050 (mmcc): Connection refused
    $ nsdo foo !!
    hi!
    hello

So (as far as I know) I have no other choice but to use veth, a kernel
feature [designed][6] to allow network namespaces to communicate. veth
interfaces act just like any interface but come in pairs -- one for each
namespace.

You can set them up with a systemd service like the following (I've
named it `foo-veth.service`):

    [Unit]
    Description=veth for foo netns
    After=netns@foo.service
    
    [Service]
    Type=oneshot
    RemainAfterExit=yes
    # configure our end
    ExecStart=/usr/bin/ip link add ns-foo up type veth peer name ns-def netns foo
    ExecStart=/usr/bin/ip addr add 10.0.255.1/24 dev ns-foo
    # configure vpn end
    ExecStart=/usr/bin/ip -netns foo link set dev ns-def up
    ExecStart=/usr/bin/ip -netns foo addr add 10.0.255.2/24 dev ns-def
    # tear down everything
    ExecStop=/usr/bin/ip link del ns-foo
    
    [Install]
    WantedBy=netns@foo.service

(Note: I've chosen not to make this example systemd service generic --
like `netns-veth@.service` -- because I currently use veth with only one
vpn/namespace and I'm not sure how I'd assign unique IP addresses
otherwise.)

Now, make `netns@foo` start the new service automatically and then (this
time) start it manually:

    # systemctl enable foo-veth
    # systemctl start foo-veth

For convenience, make the name of the namespace resolve to the IP
address assigned to its corresponding veth interface:

    # printf 'foo\t10.0.255.2\n' >>/etc/hosts

Done! You can now reach servers running in namespaces by simply
connecting to the namespace by name. If a hypothetical application
listens on port 5050 in namespace `foo`, for instance, you can access it
by pointing your client to `foo:5050`:

    $ curl foo:5050
    Hello, world!

[1]: http://naju.se/articles/openvpn-netns
[2]: https://projects.archlinux.org/svntogit/packages.git/tree/trunk/openvpn@.service?h=packages/openvpn
[3]: http://www.freedesktop.org/software/systemd/man/systemd.service.html
[4]: https://community.openvpn.net/openvpn/wiki/Openvpn23ManPage
[5]: http://blog.scottlowe.org/2013/09/04/introducing-linux-network-namespaces/
[6]: https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=e314dbdc1c0dc6a548ecf0afce28ecfd538ff568
