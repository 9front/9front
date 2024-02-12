#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_debug.h"

static u32int debug_mask;

void ext4_dmask_set(u32int m)
{
	debug_mask |= m;
}

void ext4_dmask_clr(u32int m)
{
	debug_mask &= ~m;
}

u32int ext4_dmask_get(void)
{
	return debug_mask;
}
