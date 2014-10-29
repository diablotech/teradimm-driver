#ifndef _TD_RAIDMETA__H
#define _TD_RAIDMETA__H


#define TR_METADATA_VERSION             0x2

struct __packed  tr_meta_data_struct {
#define TR_META_DATA_SIGNATURE_SIZE             64      /* @ 0 */
	struct {                /* 64  bytes */
		uint8_t         uuid[16];
		uint32_t        uuid_check;
		uint32_t        length;
		uint64_t        csum;
		uint64_t        version;
		uint64_t        state;
		uint64_t	unused[2];
	} signature;

	struct {
		uint64_t        unused[8];
	} signature_reserved;
	

#define TR_META_DATA_INFO_SIZE                  896     /* @ 128 */
#define TR_META_DATA_CONF_MAX                   56     /* @ 128 */
	struct {
		struct td_ioctl_conf_entry conf[TR_META_DATA_CONF_MAX];
	} raid_info;
	
#define TR_META_DATA_UNUSED_SIZE                1024    /* @ 1024 */
	struct {
		uint64_t unused[128];
	} info_reserved;

#define TR_META_DATA_MEMBER_SIZE                64      /* @ 2048 */
#define TR_META_DATA_MEMBERS_MAX                32
	struct {
		uint8_t         uuid[16];
		uint32_t        state;
		uint32_t        _reserved1;
		uint64_t        generation;
		uint64_t        _reserved2[4];
	} member[TR_META_DATA_MEMBERS_MAX];
};


#endif
