
/*
 *	We have to be cautious here. We have seen BIOSes with DMI pointers
 *	pointing to completely the wrong place for example
 */
static void dmi_table(u8 *buf, int len, int num,
		      void (*decode)(const struct dmi_header *, void *),
		      void *private_data)
{
	u8 *data = buf;
	int i = 0;

	/*
	 *	Stop when we see all the items the table claimed to have
	 *	OR we run off the end of the table (also happens)
	 */
	while ((i < num) && (data - buf + sizeof(struct dmi_header)) <= len) {
		const struct dmi_header *dm = (const struct dmi_header *)data;

		/*
		 *  We want to know the total length (formatted area and
		 *  strings) before decoding to make sure we won't run off the
		 *  table in dmi_decode or dmi_string
		 */
		data += dm->length;
		while ((data - buf < len - 1) && (data[0] || data[1]))
			data++;
		if (data - buf < len - 1)
			decode(dm, private_data);
		data += 2;
		i++;
	}
}
static u32 dmi_base;
static u16 dmi_len;
static u16 dmi_num;

#ifndef ioremap
#include <asm/io.h>
#endif
/**
 *	dmi_walk - Walk the DMI table and get called back for every record
 *	@decode: Callback function
 *	@private_data: Private data to be passed to the callback function
 *
 *	Returns -1 when the DMI table can't be reached, 0 on success.
 */
int dmi_walk(void (*decode)(const struct dmi_header *, void *),
	     void *private_data)
{
	u8 *buf;
/*
	if (!dmi_available)
		return -1;
*/

	buf = ioremap(dmi_base, dmi_len);
	if (buf == NULL)
		return -1;

	dmi_table(buf, dmi_len, dmi_num, decode, private_data);

	iounmap(buf);
	return 0;
}

