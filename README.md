nsdo
====

This repository holds my system configuration for running particular
applications under OpenVPN or Cisco AnyConnect VPNs on Ubuntu. Here are
my two simultaneous usecases:

 1. Run clients for some peer-to-peer protocols through OpenVPN without
    affecting other traffic, like browsing Wikipedia, which blocks edits
    from my VPN provider
 2. `ssh` into a cluster on my university's network via a Cisco
    AnyConnect VPN without sending my personal traffic through my
    university's network

Putting these two VPNs in their own [Linux network namespaces][1] and
having a command that lets me run an application in a namespace, like

    $ nsdo gatech ssh something.gatech.edu

addresses both usecases well, and that's what this repository does.

However, the story does not end with network namespaces thanks to
`/etc/resolv.conf`, which needs a different version for all three
network namespaces involved. `ip netns exec` attempts to work around
this on every execution by making a new mount namespace and
bind-mounting each file in `/etc/netns/NSNAME/` to `/etc/`. So
theoretically, to fix our `resolv.conf` problem, you would write the
VPN-specific DNS configuration to `/etc/netns/NSNAME/resolv.conf`; but
`/etc/resolv.conf` is a symlink to
`/run/systemd/resolve/stub-resolv.conf` on my system. So iproute2 ends
up bind-mounting to that link destination, killing the bind mount
when something `rename()`s that volatile file.

So this repository takes a different approach: create a single mount
namespace corresponding to the network namespace, and inside it, mount
`/var/ns-etc/NSNAME/` with [`overlayfs`][5] on top of `/etc/`. Then,
when we run something in the network namespace, `nsdo` will call
`setns()` for this mount namespace as well as the network namespace.
It mounts `/var/ns-etc/NSNAME/` as the `overlayfs` "upper layer", so
changes made in the namespace actually persist in `/var/ns-etc/NSNAME/`
rather than `/etc/`. It cannot mount `/etc/netns/NSNAME/` because
`overlayfs` [gets upset at the overlap in paths][9].

For convenience, the `nsdo` binary has the [setuid bit][2] set, giving
it root privileges, which allows it to change namespaces, `setuid()` to
the user who ran the command, and then `exec()` the requested command.

Installation
------------

Clone this repository and run:

    $ make
    $ sudo make install install-anyconnect install-openvpn

To change the default installation directory of `/usr/local`, set
`PREFIX` to something else when you call both `make` and `make install`.
Leave off any of `install-anyconnect` or `install-openvpn` if you don't
want those configurations.

### Cisco AnyConnect

I use [openconnect][6], a free-as-in-freedom client for Cisco AnyConnect
VPNs available in a distribution's repository near you.

The `install-anyconnect` target of the Makefile mentioned above will
create an `openconnect@.service` systemd unit. If you create a profile
named `gatech.conf` in `/usr/local/share/openconnect/`, you should be
able to `sudo systemctl start openconnect@gatech` and then be on your
way.

A profile (say, `/usr/local/share/openconnect/gatech.conf`) looks like
this:

    server=https://anyc.vpn.gatech.edu
    pass1=hunter2
    pass2=push
    --authgroup=gatech-2fa-Duo
    --user=aadams80

It's messy, but lines starting with `--` are long options passed
directly to `openconnect` (see `openconnect(8)` for a list of long
options). Anything else must be one of the three keys above (`server`,
`pass1`, `pass2`), which the `openconnect-wrapper` in this repository
processes and handles for you.

If `pass1=...` is missing in the profile (the better choice
security-wise), you'll need to input it with
`systemd-tty-ask-password-agent` as shown in the example below.

`pass2` is also optional. It's the second line of the password sent to
the server; the Georgia Tech VPN interprets `push` as "send me a 2FA
push notification on my phone". After I approve the 2FA request on my
phone, the VPN connects and I'm good to go.

If this setup causes trouble on your machine, please open an issue. I
want to make this robust, but I don't know much about others' VPN
configurations, so I'm making this up as I go.

#### ssh Configuration with ProxyCommand

It's easy to forget the `nsdo gatech` in front of an `ssh` command, so I
added the following to my `~/.ssh/config` (last line is the important
one):

    Host pace
        User aadams80
        HostName coc-ice.pace.gatech.edu
        IdentityFile ~/.ssh/id_rsa_pace
        IdentitiesOnly yes
        ProxyCommand /usr/local/bin/nsdo gatech /usr/bin/nc %h %p

Then I can login with simply

    $ ssh pace

#### Finished Product

    $ sudo systemctl start openconnect@gatech
    $ sudo systemd-tty-ask-password-agent    # only needed without pass1 in profile
    Password for AnyConnect VPN gatech: *******
    [Approve the 2FA request on my phone]
    $ nsdo gatech curl https://austinjadams.com/ip
    143.215.38.178
    $ whois 143.215.38.178
    ...
    OrgName:        Georgia Institute of Technology
    OrgId:          GIT-Z
    Address:        756 W Peachtree ST
    City:           Atlanta
    StateProv:      GA
    PostalCode:     30308
    Country:        US

### OpenVPN

The `install-openvpn` Makefile target above installs a systemd
[drop-in][7] configuration file for `openvpn-client@.service` at
`/usr/local/lib/systemd/system/openvpn-client@.service.d/50-netns.conf`.
If you don't have a `/usr/lib/systemd/system/openvpn-client@.service`,
[here][8]'s a link to an upstream copy.

At the bottom of a normal openvpn configuration file in
`/etc/openvpn/client/` (say, `/etc/openvpn/client/foo.conf`), you should
be able to add the following:

    # ... (rest of configuration) ...

    # script should run `ip`, not openvpn
    route-noexec
    ifconfig-noexec
    up "/usr/local/bin/openvpn-ns"
    route-up "/usr/local/bin/openvpn-ns"
    script-security 2

Then you should be able to `sudo systemctl start openvpn-client@foo`. If
you encounter problems, please open an issue because I want to
understand others' VPN/OS situations better.

#### Finished Product

    $ sudo systemctl start openvpn-client@foo
    $ nsdo foo some-graphical-p2p-application &

### Forwarding Ports into a Namespace

By design, applications cannot connect to ports bound in other network
namespaces. So if you have a server running in some other network
namespace with nsdo (e.g., a headless peer-to-peer client), you cannot
connect to it from the default network namespace. For example:

    $ nsdo foo nc -l -p 6969 <<<"hi!" &
    $ nc -v localhost 6969 <<<"hello"
    localhost [127.0.0.1] 6969: Connection refused
    $ nsdo foo nc -v localhost 6969 <<<"hello"
    hi!
    hello

You can work around this using `veth`, a kernel feature [designed][10] to
allow network namespaces to communicate. veth interfaces act just like
any interface but come in pairs — one for each namespace.

#### The veth systemd unit

I added a new systemd unit, `foo-veth.service` in `/etc/systemd/system/`
that looks like this:

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

I would make this a template unit named `veth@.service` and commit it to
this repository so you can install it with the Makefile, but I am not
sure how best to allocate IP address spaces (e.g., `10.0.255.0/24`)
based off the instance name (e.g., `foo`). Once I created that, though,
I enabled the unit (`--now` will start it right now):

    # systemctl daemon-reload
    # systemctl enable --now foo-veth

Now, if you run `ip link` both inside and outside the namespace, you can
see the veth interfaces:

    $ ip link
    ...
    12: ns-foo@if3: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP mode DEFAULT group default qlen 1000
        link/ether ee:05:c1:aa:83:26 brd ff:ff:ff:ff:ff:ff link-netns foo
    $ nsdo foo ip link
    ...
    3: ns-def@if12: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP mode DEFAULT group default qlen 1000
        link/ether ee:e1:0b:b9:6b:6f brd ff:ff:ff:ff:ff:ff link-netnsid 0

For convenience, I would add the name of the namespace to `/etc/hosts`:

    10.0.255.2	foo

Now the example from earlier "works", like this:

    $ nsdo foo nc -l -p 6970 <<<"hi!" &
    $ nc -v foo 6970 <<<"hello"
    hi!
    hello

#### Configuring the Server

Now, suppose we have a more realistic situation: we want to run a server
in the namespace that isn't just an instance of netcat, like an HTTP
server. Assuming the server has some systemd unit
`original-server.service`, you can add a drop-in configuration file for
it at `/etc/systemd/system/original-server.service.d/50-netns.conf` as
follows (`/usr/bin/the-original-server --original --args` is the
original command line from `/lib/systemd/system/original-server.service`
or wherever):

    [Unit]
    Requires=netns@foo.service
    After=netns@foo.service

    [Service]
    ExecStart=
    ExecStart=/usr/local/bin/nsdo foo /usr/bin/the-original-server --original --args

Then start it up:

    # systemctl daemon-reload
    # systemctl restart original-server

Now you should be able to access it from the default network namespace.
For example, if it's an HTTP server listening on port 6969:

    $ curl http://foo:6969/
    hello, world!

#### Port forwarding with iptables

Suppose the server is now peacefully listening on port 6969 in the `foo`
network namespace. If you want other machines on the network to be able
to access that server via port 6969 on the host machine, you can use
iptables:

    # iptables -A PREROUTING ! -s 10.0.255.0/24 -p tcp -m tcp --dport 6969 -j DNAT --to-destination 10.0.255.2
    # iptables -A POSTROUTING -o ns-foo -j MASQUERADE
    # iptables-save

Now, on another machine, we should be able to access the machine
running the server:

    $ curl http://originalmachine:6969/
    hello, world!

License
-------
[MIT/X11 License][3]

[1]: https://lwn.net/Articles/580893/
[2]: https://en.wikipedia.org/wiki/Setuid
[3]: https://github.com/ausbin/nsdo/blob/master/LICENSE
[4]: https://austinjadams.com/blog/running-select-applications-through-openvpn/
[5]: https://www.kernel.org/doc/html/latest/filesystems/overlayfs.html
[6]: https://www.infradead.org/openconnect/
[7]: https://www.freedesktop.org/software/systemd/man/systemd.unit.html
[8]: https://github.com/OpenVPN/openvpn/blob/452e016cba977cb1c109e74977029b9c0de33de2/distro/systemd/openvpn-client%40.service.in
[9]: https://unix.stackexchange.com/q/578196/62375
[10]: https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=e314dbdc1c0dc6a548ecf0afce28ecfd538ff568

Manpage
-------

    nsdo(1)               General Commands Manual              nsdo(1)
    
    NAME
           nsdo - run a command in a network namespace
    
    SYNOPSIS
           nsdo namespace command [args ...]
    
           nsdo { --version | -V }
    
    DESCRIPTION
           Execute  command  as the current user/group in namespace, a
           Linux network namespace set up with the accompanying  netns
           script or iproute2 (see ip-netns(8)).
    
           By  default, netns and iproute2 place network namespaces in
           /var/run/netns/, so nsdo searces for namespaces there  (in‐
           cluding  namespace).  netns also places mount namespaces in
           /var/run/mountns/     corresponding     to     those     in
           /var/run/netns/,  so  in addition to the network namespace,
           nsdo will enter into a corresponding mount namespace if  it
           exists. This way, files in /var/ns-etc/NSNAME/ will show up
           at /etc/ for applications run using nsdo NSNAME;  this  no‐
           tably includes resolv.conf.
    
           To  prevent  command  from  easily  escaping  the namespace
           'jail,' nsdo will exit if the current namespace  exists  in
           that  directory.   Consequently, you can not nest instances
           of nsdo.
    
    OPTIONS
           --version, -V
                  Instead of running a command, print  nsdo's  version
                  and exit.
    
    SEE ALSO
           ip(8), ip-netns(8), namespaces(7), nsenter(1)
    
                                2020-01-23                     nsdo(1)
