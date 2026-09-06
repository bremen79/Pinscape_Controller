// Pinscape Controller firmware version
//
// The firmware has never carried a version number: official builds were named
// by date, and only one person produced them, so the build date served as the
// identifier. That stopped being enough once the firmware could be built
// locally again -- two people can now compile different code on the same day,
// or the same code on different days.
//
// The numbering continues the V2 generation rather than starting over. V1 and
// V2 already name the two software generations the README describes, and
// nothing here breaks the V2 model: the Config Tool, the flashing procedure
// and the configuration format are all unchanged. So this is 2.1.0 -- the V2
// generation, plus the Open Pinball Device interface.
//
// Firmware that predates this field reports 0.0.0, which is what a host reads
// when the version is unavailable, so the two are never confused.
//
// The build date remains available separately, through the existing
// "65 10" query, and answers a different question: which binary, rather than
// which release.

#ifndef PINSCAPE_VERSION_H
#define PINSCAPE_VERSION_H

#define VERSION_MAJOR  2
#define VERSION_MINOR  1
#define VERSION_PATCH  0

#endif
