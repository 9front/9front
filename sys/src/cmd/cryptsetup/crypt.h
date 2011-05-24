// Author Taru Karttunen <taruti@taruti.net>
// This file can be used as both Public Domain or Creative Commons CC0.
#include <libsec.h>

typedef struct {
	unsigned char Salt[16];
	unsigned char Key[32];
} Slot;

typedef struct {
	unsigned char Master[32];
	Slot Slots[8];
	AESstate C1, C2;
} XtsState;
