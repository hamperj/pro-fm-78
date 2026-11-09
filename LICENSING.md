# Licensing

PRO-FM-78 is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). If you give someone a firmware built from it, give them its complete source under the same licence.

## Who made what

| | |
| --- | --- |
| The module: `profm78/profm78_module.c`, `profm78_play.inc`, `profm78_melodee.inc`, `profm78_types.h`, `take.py`; `tests/`, `build.sh` | James Hampshire |
| The engine `profm78/p5/` (taken unchanged by `take.py`) | Kerem Kilic (Ellic Studio), [Melodee](https://github.com/keremimo/melodee) 1.6.1 (165d177), GPL-3.0-only. Its own notes: `profm78/p5/LICENSING.md`, `LICENSE` |
| The factory programs `profm78/p5/melodee_prophet_factory.h` | Sequential's patch data, as Melodee bundles it, unchanged. The software licence does not relicense this data: see `profm78/p5/FACTORY.md` |
| The firmware around the module (host, platform, update loader, tools) | [fm1-dock](https://github.com/joakimeriksson/fm1-dock) (Joakim Eriksson), on [Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton Instruments) and [SLOOP](https://github.com/isod89/sloop-fm1) (isod89): see fm1-dock's `LICENSING.md` |
| `patches/fm1-dock-uc25-flash.patch` | After Felucca f4cbb6a (the UC25HQ80 flash) |

Product names used here are their owners' trademarks. PRO-FM-78 is not affiliated with or endorsed by them.
