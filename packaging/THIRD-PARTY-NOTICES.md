# Third-party notices

SidPlayZero is licensed under the GNU General Public License, version 3 or later (see `LICENSE`). The
program in this package also contains the work of others, under their own licences, which are given
below. Each is compatible with the GPL, version 3.

Built into the program: LVGL, the engine (libsidplayfp and libresidfp), miniaudio, and fonts made from
DejaVu Sans. doctest is used by the tests only and is not in the program.

## LVGL 9.5.0

The graphics library that draws the screens. <https://github.com/lvgl/lvgl>

```
MIT licence
Copyright (c) 2025 LVGL Kft

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Montserrat

The typeface of the text, in the form of the bitmap fonts that come with LVGL. Copyright the Montserrat
Project Authors (<https://github.com/JulietaUla/Montserrat>). Licensed under the SIL Open Font License,
version 1.1: <https://openfontlicense.org/open-font-license-official-text/>

## libsidplayfp 3.1.2

The SID engine that plays the tunes, built into the program from `libsidplayfp-3.1.2.tar.gz`
(<https://github.com/libsidplayfp/libsidplayfp>). Copyright 2011-2025 Leandro Nini, 2007-2010 Antti
Lankila, 2000-2001 Simon White, and the other authors its files name.

Licensed under the GNU General Public License, version 2 or (at your option) any later version
(GPL-2.0-or-later). SidPlayZero uses it under version 3 of that licence, whose text is the file `LICENSE`
beside this one. Its source is the release archive named above, built unchanged.

## libresidfp 1.3.0

The emulation of the SID chip that libsidplayfp's reSIDfp engine uses, built into the program from
`libresidfp-1.3.0.tar.gz` (<https://github.com/libsidplayfp/libresidfp>). Copyright 2011-2026 Leandro Nini,
2007-2010 Antti Lankila, 2004 Dag Lem, and the other authors its files name.

Licensed under the GNU General Public License, version 2 or (at your option) any later version
(GPL-2.0-or-later). SidPlayZero uses it under version 3 of that licence, whose text is the file `LICENSE`
beside this one. Its source is the release archive named above, built unchanged.

## miniaudio 0.11.25

The library that hands the sound to the system (<https://miniaud.io>), built into the program from its one
header. It is offered as public domain or, as used here, under the MIT No Attribution licence:

```
MIT No Attribution
Copyright 2026 David Reid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## DejaVu Sans

The typeface of the text, as bitmap fonts at 12 and 14 pixels generated from DejaVu Sans
(<https://dejavu-fonts.github.io/>). DejaVu is based on the Bitstream Vera fonts, whose licence it keeps:

```
Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera is a trademark of Bitstream, Inc.
DejaVu changes are in public domain.

Permission is hereby granted, free of charge, to any person obtaining a copy of the fonts accompanying this license ("Fonts") and associated documentation files (the "Font Software"), to reproduce and distribute the Font Software, including without limitation the rights to use, copy, merge, publish, distribute, and/or sell copies of the Font Software, and to permit persons to whom the Font Software is furnished to do so, subject to the following conditions:

The above copyright and trademark notices and this permission notice shall be included in all copies of one or more of the Font Software typefaces.

The Font Software may be modified, altered, or added to, and in particular the designs of glyphs or characters in the Fonts may be modified and additional glyphs or characters may be added to the Fonts, only if the fonts are renamed to names not containing either the words "Bitstream" or the word "Vera".

This License becomes null and void to the extent applicable to Fonts or Font Software that has been modified and is distributed under the "Bitstream Vera" names.

The Font Software may be sold as part of a larger software package but no copy of one or more of the Font Software typefaces may be sold by itself.

THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL BITSTREAM OR THE GNOME FOUNDATION BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM OTHER DEALINGS IN THE FONT SOFTWARE.

Except as contained in this notice, the names of Gnome, the Gnome Foundation, and Bitstream Inc., shall not be used in advertising or otherwise to promote the sale, use or other dealings in this Font Software without prior written authorization from the Gnome Foundation or Bitstream Inc., respectively. For further information, contact: fonts at gnome dot org.
```
