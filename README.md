nsdo
====

`nsdo` (network namespace do) is a simple C program that runs a command
inside a given [Linux network namespace][1].

Effectively, it simplifies:

    $ sudo ip netns exec myns sudo -u $USER myprogram

to

    $ nsdo myns myprogram

Thanks to magic of the [setuid bit][2], it initially has root
privileges, which allows it to change its own network namespace,
`setuid()` to the user who ran the command, and then `exec()` the
requested command.

installation
------------

If you're on Arch, you can build [my AUR package][4].

Otherwise:

    $ make
    # make install

To change the default installation directory of `/usr/local`, set
`PREFIX` to something else when you call `make install`.

openvpn example
---------------

I wrote this program because I run some applications under a VPN (e.g.,
clients for peer-to-peer protocols) and leave others untouched (like a
game client).

For more details, see `openvpn-example.md` ([cgit][5], [github][6]).

license
-------
[MIT/X11][3].

[1]: https://lwn.net/Articles/580893/
[2]: https://en.wikipedia.org/wiki/Setuid
[3]: https://github.com/ausbin/nsdo/blob/master/LICENSE
[4]: https://aur.archlinux.org/packages/nsdo-git/
[5]: https://code.austinjadams.com/nsdo/plain/openvpn-example.md
[6]: https://github.com/ausbin/nsdo/blob/master/openvpn-example.md

manpage
-------

    nsdo(1)               General Commands Manual              nsdo(1)
    
    NAME
           nsdo - run a command in a network namespace
    
    SYNOPSIS
           nsdo namespace command [args ...]
    
           nsdo { --version | -V }
    
    DESCRIPTION
           Execute  command  as the current user/group in namespace, a
           Linux network namespace  set  up  with  iproute2  (see  ip-
           netns(8)).
    
           By   default,   iproute2   places   network  namespaces  in
           /var/run/netns/,  so  nsdo  searces  for  namespaces  there
           (including  namespace).   To  prevent  command  from easily
           escaping the namespace 'jail,' nsdo will exit if  the  cur‐
           rent namespace exists in that directory.  Consequently, you
           can not nest instances of nsdo.
    
    OPTIONS
           --version, -V
                  Instead of running a command, print  nsdo's  version
                  and exit.
    
    SEE ALSO
           ip(8), ip-netns(8), namespaces(7)
    
                                2016-01-23                     nsdo(1)
