#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_debug.h"
#include "ext4_mbr.h"

#define MBR_SIGNATURE 0xAA55

#pragma pack on

struct ext4_part_entry {
	u8int status;
	u8int chs1[3];
	u8int type;
	u8int chs2[3];
	u32int first_lba;
	u32int sectors;
};

struct ext4_mbr {
	u8int bootstrap[442];
	u32int disk_id;
	struct ext4_part_entry part_entry[4];
	u16int signature;
};

#pragma pack off

int ext4_mbr_scan(struct ext4_blockdev *parent, struct ext4_mbr_bdevs *bdevs)
{
	int r;
	usize i;

	ext4_dbg(DEBUG_MBR, DBG_INFO "ext4_mbr_scan\n");
	memset(bdevs, 0, sizeof(struct ext4_mbr_bdevs));
	r = ext4_block_init(parent);
	if (r != 0)
		return r;

	r = ext4_block_readbytes(parent, 0, parent->bdif->ph_bbuf, 512);
	if (r != 0) {
		goto blockdev_fini;
	}

	const struct ext4_mbr *mbr = (void *)parent->bdif->ph_bbuf;

	if (to_le16(mbr->signature) != MBR_SIGNATURE) {
		ext4_dbg(DEBUG_MBR, DBG_ERROR "ext4_mbr_scan: unknown "
			 "signature: 0x%x\n", to_le16(mbr->signature));
		werrstr(Enotfound);
		r = -1;
		goto blockdev_fini;
	}

	/*Show bootstrap code*/
	ext4_dbg(DEBUG_MBR, "mbr_part: bootstrap:");
	for (i = 0; i < sizeof(mbr->bootstrap); ++i) {
		if (!(i & 0xF))
				ext4_dbg(DEBUG_MBR | DEBUG_NOPREFIX, "\n");
		ext4_dbg(DEBUG_MBR | DEBUG_NOPREFIX, "%02x, ", mbr->bootstrap[i]);
	}

	ext4_dbg(DEBUG_MBR | DEBUG_NOPREFIX, "\n\n");
	for (i = 0; i < 4; ++i) {
		const struct ext4_part_entry *pe = &mbr->part_entry[i];
		ext4_dbg(DEBUG_MBR, "mbr_part: %d\n", (int)i);
		ext4_dbg(DEBUG_MBR, "\tstatus: 0x%x\n", pe->status);
		ext4_dbg(DEBUG_MBR, "\ttype 0x%x:\n", pe->type);
		ext4_dbg(DEBUG_MBR, "\tfirst_lba: 0x%ux\n", pe->first_lba);
		ext4_dbg(DEBUG_MBR, "\tsectors: 0x%ux\n", pe->sectors);

		if (!pe->sectors)
			continue; /*Empty entry*/

		if (pe->type != 0x83)
			continue; /*Unsupported entry. 0x83 - linux native*/

		bdevs->partitions[i].bdif = parent->bdif;
		bdevs->partitions[i].part_offset =
			(u64int)pe->first_lba * parent->bdif->ph_bsize;
		bdevs->partitions[i].part_size =
			(u64int)pe->sectors * parent->bdif->ph_bsize;
	}

	blockdev_fini:
	ext4_block_fini(parent);
	return r;
}

int ext4_mbr_write(struct ext4_blockdev *parent, struct ext4_mbr_parts *parts, u32int disk_id)
{
	int r;
	u64int disk_size;
	u32int division_sum = parts->division[0] + parts->division[1] +
				parts->division[2] + parts->division[3];

	if (division_sum > 100) {
		werrstr(Einval);
		return -1;
	}

	ext4_dbg(DEBUG_MBR, DBG_INFO "ext4_mbr_write\n");
	r = ext4_block_init(parent);
	if (r != 0)
		return r;

	disk_size = parent->part_size;

	/*Calculate CHS*/
	u32int k = 16;
	while ((k < 256) && ((disk_size / parent->bdif->ph_bsize / k / 63) > 1024))
		k *= 2;

	if (k == 256)
		--k;

	const u32int cyl_size = parent->bdif->ph_bsize * 63 * k;
	const u32int cyl_count = disk_size / cyl_size;

	struct ext4_mbr *mbr = (void *)parent->bdif->ph_bbuf;
	memset(mbr, 0, sizeof(struct ext4_mbr));

	mbr->disk_id = disk_id;

	u32int cyl_it = 0;
	for (int i = 0; i < 4; ++i) {
		u32int cyl_part = cyl_count * parts->division[i] / 100;
		if (!cyl_part)
			continue;

		u32int part_start = cyl_it * cyl_size;
		u32int part_size = cyl_part * cyl_size;

		if (i == 0) {
			part_start += 63;
			part_size -= 63 * parent->bdif->ph_bsize;
		}

		u32int cyl_end = cyl_part + cyl_it - 1;

		mbr->part_entry[i].status = 0;
		mbr->part_entry[i].chs1[0] = i ? 0 : 1;;
		mbr->part_entry[i].chs1[1] = ((cyl_it >> 2) & 0xC0) + 1;
		mbr->part_entry[i].chs1[2] = cyl_it & 0xFF;
		mbr->part_entry[i].type = 0x83;
		mbr->part_entry[i].chs2[0] = k - 1;
		mbr->part_entry[i].chs2[1] = ((cyl_end >> 2) & 0xC0) + 63;
		mbr->part_entry[i].chs2[2] = cyl_end & 0xFF;

		mbr->part_entry[i].first_lba = part_start;
		mbr->part_entry[i].sectors = part_size / parent->bdif->ph_bsize;

		cyl_it += cyl_part;
	}

	mbr->signature = MBR_SIGNATURE;
	r = ext4_block_writebytes(parent, 0, parent->bdif->ph_bbuf, 512);
	if (r != 0)
		goto blockdev_fini;


	blockdev_fini:
	ext4_block_fini(parent);
	return r;
}
