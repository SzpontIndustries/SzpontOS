# Archived terminal implementation

`curses.c.txt` preserves the former standalone curses implementation. It was not
listed in `libc/Makefile` and its private WINDOW fields are incompatible with the
GNU ncurses headers installed in the sysroot. It is archived as text so source
checks do not compile it against an unrelated ABI.

The supported curses implementation is `third_party/ncurses`, built by
`mk/third_party.mk`; userland links against its shared library. Do not add this
archived implementation to libc or export another set of curses symbols.
