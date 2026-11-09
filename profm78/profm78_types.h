/* SPDX-License-Identifier: GPL-3.0-only */
/* PRO-FM-78's types: one for each of Sequential's 200 factory programs, chosen from its name (the ALGORITHM knob
 * steps through them, PRESETS through a type's programs). Move a program by changing its line. */
enum { T_BASS, T_LEAD, T_POLY, T_PAD, T_STRINGS, T_BRASS, T_WIND, T_KEYS, T_ORGAN, T_PLUCK, T_BELLS, T_PERC, T_FX, T_N };
static const char *const TYPE_NAME[T_N] = {"BASS", "LEAD", "POLY", "PAD", "STRINGS", "BRASS", "WIND", "KEYS", "ORGAN",
                                          "PLUCK", "BELLS", "PERC", "FX"};
static const uint8_t TYPE_OF[200] = {
    T_POLY,     /* 111 It's a Prophet 5 */
    T_BELLS,    /* 112 After Ringer */
    T_KEYS,     /* 113 Forever Keys */
    T_BASS,     /* 114 Funk Bass II */
    T_PAD,      /* 115 Formant Seraphim */
    T_BRASS,    /* 116 Stabby Brass */
    T_LEAD,     /* 117 No Whining */
    T_POLY,     /* 118 Fantastic Five */
    T_LEAD,     /* 121 Velocipeed */
    T_ORGAN,    /* 122 PDG Organ */
    T_KEYS,     /* 123 Clavicle */
    T_LEAD,     /* 124 Blow by Blow */
    T_PAD,      /* 125 TaariChoir */
    T_WIND,     /* 126 Digi-Recorder */
    T_STRINGS,  /* 127 Reedy String Pad */
    T_POLY,     /* 128 Canned Happiness */
    T_LEAD,     /* 131 Synchrotrill */
    T_POLY,     /* 132 Born in the 80's */
    T_PLUCK,    /* 133 Sticky Plucks */
    T_PLUCK,    /* 134 Watery Plucks */
    T_BASS,     /* 135 Bass Face */
    T_BELLS,    /* 136 Antique Bell */
    T_LEAD,     /* 137 Whiny Opener */
    T_PLUCK,    /* 138 Synctink */
    T_ORGAN,    /* 141 Trembling Organ */
    T_FX,       /* 142 Sci-Fi Echo */
    T_PAD,      /* 143 Unicorn Dreams */
    T_KEYS,     /* 144 Cocktail Hour */
    T_PAD,      /* 145 Nice n Smooth */
    T_BASS,     /* 146 Tumescent Bass */
    T_PAD,      /* 147 Internalized */
    T_BELLS,    /* 148 Blinker Bells */
    T_FX,       /* 151 Pickle Pincher */
    T_FX,       /* 152 Underwater Trill */
    T_FX,       /* 153 Fairy Dust */
    T_PERC,     /* 154 The Hats */
    T_PERC,     /* 155 Congo Bongo */
    T_PERC,     /* 156 SurdoTaiko */
    T_BELLS,    /* 157 Punchy Bells */
    T_PAD,      /* 158 Mindfull */
    T_POLY,     /* 211 Poly McFat */
    T_PAD,      /* 212 Tangerine Chrch Thm */
    T_KEYS,     /* 213 Reed EP */
    T_STRINGS,  /* 214 Cars Strings */
    T_ORGAN,    /* 215 Pipe Organisation */
    T_BASS,     /* 216 Fat Poly Bass */
    T_PAD,      /* 217 Choral Voices */
    T_BELLS,    /* 218 Small Gong */
    T_PAD,      /* 221 Another Ocean */
    T_PAD,      /* 222 Pedaling Sweep */
    T_LEAD,     /* 223 Hum-Along Solo */
    T_BASS,     /* 224 Fat Bass Stack */
    T_BRASS,    /* 225 Afterblow Horn */
    T_PAD,      /* 226 Midnight PWM */
    T_POLY,     /* 227 Hyper Poly */
    T_FX,       /* 228 Ripples */
    T_POLY,     /* 231 Aggro Synth */
    T_PAD,      /* 232 No Parking Swell */
    T_FX,       /* 233 Radio Towers */
    T_KEYS,     /* 234 Harpsi-Vibe */
    T_PAD,      /* 235 Instability Pad */
    T_KEYS,     /* 236 Vintage Wurly */
    T_POLY,     /* 237 Phazy Synth */
    T_FX,       /* 238 Robot Hamsters */
    T_PAD,      /* 241 Pressure Points */
    T_STRINGS,  /* 242 80s Horror Strings */
    T_POLY,     /* 243 NurseryComp */
    T_POLY,     /* 244 House Mover */
    T_BELLS,    /* 245 Carillons */
    T_PAD,      /* 246 Vocal Pad */
    T_BRASS,    /* 247 Soft Moving Brass */
    T_LEAD,     /* 248 Squarecliff */
    T_PERC,     /* 251 J3 808 */
    T_FX,       /* 252 CATS MEOW */
    T_LEAD,     /* 253 Morse Code Lead */
    T_FX,       /* 254 DOG */
    T_PAD,      /* 255 Under The Ice */
    T_PAD,      /* 256 Distant Future */
    T_BELLS,    /* 257 Marimba */
    T_LEAD,     /* 258 Duotone */
    T_PLUCK,    /* 311 Pulse Pluck */
    T_LEAD,     /* 312 Unisawyer */
    T_WIND,     /* 313 Recorder */
    T_FX,       /* 314 Hoof Gelatin */
    T_PAD,      /* 315 Ghostly Glow */
    T_BASS,     /* 316 Uptight Bass */
    T_POLY,     /* 317 Ravecave */
    T_LEAD,     /* 318 Dirty Lead */
    T_WIND,     /* 321 Celtic Moon */
    T_PAD,      /* 322 Hollow Dreams */
    T_KEYS,     /* 323 Furry Tines */
    T_FX,       /* 324 Mouse Blaster */
    T_PLUCK,    /* 325 Kyoto */
    T_BRASS,    /* 326 Pink Neon Brass */
    T_ORGAN,    /* 327 Organ Perc */
    T_LEAD,     /* 328 Skreamer Solo */
    T_PAD,      /* 331 Dream of Orgonon */
    T_KEYS,     /* 332 Bistro */
    T_BASS,     /* 333 Impact Bass */
    T_PLUCK,    /* 334 Pluck Hollow */
    T_LEAD,     /* 335 Velo Res Lead 2 */
    T_BRASS,    /* 336 Soft Brass */
    T_BRASS,    /* 337 Regal Movement */
    T_POLY,     /* 338 Loose Squares */
    T_STRINGS,  /* 341 High Strings */
    T_KEYS,     /* 342 Harpsicrud */
    T_BASS,     /* 343 Rubber Bumper */
    T_PERC,     /* 344 Galvanized Bucket */
    T_PAD,      /* 345 Soft Swell */
    T_STRINGS,  /* 346 Reverberant Strings */
    T_BASS,     /* 347 Lagoon Bass */
    T_PLUCK,    /* 348 Pluckity Duck */
    T_BELLS,    /* 351 Bell Tree */
    T_FX,       /* 352 Riser Chord */
    T_PERC,     /* 353 Timpanally */
    T_PERC,     /* 354 Rod Strike */
    T_PERC,     /* 355 Tuned Percussion */
    T_BELLS,    /* 356 Mystery Bell */
    T_WIND,     /* 357 FM Clarinet */
    T_BELLS,    /* 358 Funky Rev1 Bells */
    T_PLUCK,    /* 411 Sitarama */
    T_LEAD,     /* 412 FedBack */
    T_PLUCK,    /* 413 Nylon Fingers */
    T_BELLS,    /* 414 Gamelan */
    T_BELLS,    /* 415 Sparkle Ponies */
    T_FX,       /* 416 Marrakesh Radio C */
    T_PAD,      /* 417 Faint Memories */
    T_POLY,     /* 418 Full Hexagon */
    T_STRINGS,  /* 421 Old String Machine */
    T_BELLS,    /* 422 Mantel Clock */
    T_BRASS,    /* 423 Iconic Brass */
    T_PAD,      /* 424 Sync Swellpad */
    T_PLUCK,    /* 425 Pretty Pluck */
    T_POLY,     /* 426 House Vibez */
    T_BASS,     /* 427 Feature My Bass */
    T_PAD,      /* 428 ChorusLayer */
    T_POLY,     /* 431 Better Ways */
    T_LEAD,     /* 432 Night Caller */
    T_BASS,     /* 433 Bassment */
    T_PAD,      /* 434 After the Denouement */
    T_FX,       /* 435 Rewind */
    T_LEAD,     /* 436 Syncwave */
    T_ORGAN,    /* 437 Organizer */
    T_PLUCK,    /* 438 Maneater Ping */
    T_ORGAN,    /* 441 Leslied */
    T_BRASS,    /* 442 Bull By The Horns */
    T_LEAD,     /* 443 Naive Melody */
    T_POLY,     /* 444 Poly Morgue */
    T_POLY,     /* 445 Busted and Dusted */
    T_POLY,     /* 446 Switchback */
    T_BASS,     /* 447 Tight Bass (556?) */
    T_PLUCK,    /* 448 Pluckyness */
    T_FX,       /* 451 80s Sci Fi Drop */
    T_FX,       /* 452 Catfood */
    T_POLY,     /* 453 Houndstooth */
    T_FX,       /* 454 Cyberian Winds */
    T_BELLS,    /* 455 Ominous Bellfall */
    T_PLUCK,    /* 456 FM Pluck */
    T_FX,       /* 457 Metal Dronespace */
    T_PAD,      /* 458 End Credits */
    T_BRASS,    /* 511 BRASS */
    T_STRINGS,  /* 512 LOW STRINGS */
    T_KEYS,     /* 513 MUTED CLAVINET */
    T_KEYS,     /* 514 PERCUSSIVE E PIANO */
    T_WIND,     /* 515 FLUTES */
    T_KEYS,     /* 516 HARPSICHORD */
    T_LEAD,     /* 517 SYNC I */
    T_ORGAN,    /* 518 PERCUSSIVE ORGAN */
    T_LEAD,     /* 521 UNISON GLIDE W RES */
    T_ORGAN,    /* 522 HARMONIUM */
    T_ORGAN,    /* 523 ORGAN W RESONANCE */
    T_KEYS,     /* 524 TOY PIANO */
    T_WIND,     /* 525 TRUMPET FLUTE */
    T_FX,       /* 526 FILTER MOD */
    T_ORGAN,    /* 527 REED ORGAN */
    T_BASS,     /* 528 BASS IN FIFTHS */
    T_ORGAN,    /* 531 PIPE ORGAN FLUTES */
    T_LEAD,     /* 532 SYNC II */
    T_KEYS,     /* 533 ELECTRIC PIANO I */
    T_STRINGS,  /* 534 HIGH STRINGS */
    T_POLY,     /* 535 OCTAVE SAWTEETH */
    T_FX,       /* 536 RELEASE REPEAT */
    T_FX,       /* 537 DELAYED HARMONIC */
    T_FX,       /* 538 ECHO REPEAT */
    T_PAD,      /* 541 PULSE WIDTH MOD */
    T_PAD,      /* 542 SLOW SYNC SWEEP */
    T_POLY,     /* 543 FOURTHS W RESONANCE */
    T_FX,       /* 544 SWEEPING HARMONICS */
    T_LEAD,     /* 545 SLOW SYNC */
    T_FX,       /* 546 RANDOM ARPEGGIATOR */
    T_FX,       /* 547 SAWTOOTH ARPEGGIATOR */
    T_BELLS,    /* 548 CLANGOROUS BELLS */
    T_FX,       /* 551 ALIEN */
    T_FX,       /* 552 NOISE SWEEP */
    T_BELLS,    /* 553 DESCENDING BELLS */
    T_FX,       /* 554 DESCENDING PWM */
    T_FX,       /* 555 HELICOPTER */
    T_BELLS,    /* 556 RESONANCE BELLS */
    T_PAD,      /* 557 HOLLOW SOUND */
    T_FX,       /* 558 CAT */
};
