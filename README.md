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
options). Anything else must be one of the three keys above (`server,
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

Then you should be able to `sudo systemctl start openvpn@foo`. If you
encounter problems, please open an issue because I want to understand
others' VPN/OS situations better.

#### Finished Product

    $ sudo systemctl start openvpn@foo
    $ nsdo foo some-graphical-p2p-application &

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
