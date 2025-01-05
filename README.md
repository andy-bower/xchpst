# eXtended CHange Process STate

`xchpst` extends _runit_'s `chpst` tool with options for hardening runit-supervised services with Linux facilities including:

- namespaces
- capabilities
- read-only bind mounts
- isolated and transient areas of the filesystem hierarchy

`xchpst` is written from scratch and is backwards compatible with the `chpst` command line options.

The documentation effort for this tool is focussed on the [man page](xchpts.8).

Issues and merge requests welcome on the [project page](https://gitlab.com/abower/xchpst).

Releases will take the form of git tags signed by my RSA/4096 PGP key [`06AB 786E 936C 6C73 F6D8 130C 4510 3394 30FC 9F34`](https://sw.cdefg.uk/xchpst/xchpst-signing-keys.gpg).

The [CHANGELOG](CHANGELOG) represents the notable net differences between releases. Semantic versioning is employed. Versions numbered 0.x.y do not have a stable interface.

Thanks for your interest - please send feedback!

-- Andrew Bower, 1 January 2025.

## Build dependencies

* GNU make
* gcc-12
* libcap-dev
