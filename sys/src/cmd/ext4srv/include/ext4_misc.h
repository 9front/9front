#pragma once

#define EXT4_DIV_ROUND_UP(x, y) (((x) + (y) - 1)/(y))
#define EXT4_ALIGN(x, y) ((y) * EXT4_DIV_ROUND_UP((x), (y)))

/****************************Endian conversion*****************/

static inline u64int reorder64(u64int n)
{
	return  ((n & 0xff) << 56) |
		((n & 0xff00) << 40) |
		((n & 0xff0000) << 24) |
		((n & 0xff000000LL) << 8) |
		((n & 0xff00000000LL) >> 8) |
		((n & 0xff0000000000LL) >> 24) |
		((n & 0xff000000000000LL) >> 40) |
		((n & 0xff00000000000000LL) >> 56);
}

static inline u32int reorder32(u32int n)
{
	return  ((n & 0xff) << 24) |
		((n & 0xff00) << 8) |
		((n & 0xff0000) >> 8) |
		((n & 0xff000000) >> 24);
}

static inline u16int reorder16(u16int n)
{
	return  ((n & 0xff) << 8) |
		((n & 0xff00) >> 8);
}

#ifdef CONFIG_BIG_ENDIAN
#define to_le64(_n) reorder64(_n)
#define to_le32(_n) reorder32(_n)
#define to_le16(_n) reorder16(_n)

#define to_be64(_n) (_n)
#define to_be32(_n) (_n)
#define to_be16(_n) (_n)

#else
#define to_le64(_n) (_n)
#define to_le32(_n) (_n)
#define to_le16(_n) (_n)

#define to_be64(_n) reorder64(_n)
#define to_be32(_n) reorder32(_n)
#define to_be16(_n) reorder16(_n)
#endif

/****************************Access macros to ext4 structures*****************/

#define ext4_get32(s, f) to_le32((s)->f)
#define ext4_get16(s, f) to_le16((s)->f)
#define ext4_get8(s, f) (s)->f

#define ext4_set32(s, f, v) \
	do { \
		(s)->f = to_le32(v); \
	} while (0)
#define ext4_set16(s, f, v) \
	do { \
		(s)->f = to_le16(v); \
	} while (0)
#define ext4_set8 \
	(s, f, v) do { (s)->f = (v); } \
	while (0)

/****************************Access macros to jbd2 structures*****************/

#define jbd_get32(s, f) to_be32((s)->f)
#define jbd_get16(s, f) to_be16((s)->f)
#define jbd_get8(s, f) (s)->f

#define jbd_set32(s, f, v) \
	do { \
		(s)->f = to_be32(v); \
	} while (0)
#define jbd_set16(s, f, v) \
	do { \
		(s)->f = to_be16(v); \
	} while (0)
#define jbd_set8 \
	(s, f, v) do { (s)->f = (v); } \
	while (0)
