# lgt8_mod_player
Play modules soundtracks on a LGT8F32p.

This is a quick and dirty port of [pocketmod](https://github.com/rombankzero/pocketmod) to LGT8F328p.

It is limited to 4 channels .MOD [modules](https://en.wikipedia.org/wiki/MOD_(file_format)), with 15 to 31 instruments.

The audio is outputed through the digital to analog output (DAC0) to which you may want to connect an audio amplifier.
The sketch is designed to adapt automatically to the `F_CPU`, and at 32MHz, it will play at ~12KHz sample rate.

Several modules are included as a demo for test and debug purpose. Their original authors can be retreived using the link into the .txt files, into the `/mods` directory.

If you want to play a tune of your choice, it has to be `.MOD`, 4 channels, and small ennough to fit into the flash.
The module file has to be converted from binary to an 8bits array in a C header (I don't rememebr how I did that ...), and then it is compiled with the rest of the sketch and is stored as PROGMEM.

