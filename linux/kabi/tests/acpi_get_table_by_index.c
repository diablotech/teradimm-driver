#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/acpi.h>
#include <acpi/actypes.h>

int foo (void)
{
	struct acpi_table_header *tbl;
	acpi_status status;
	int i = 0;
	
	status = acpi_get_table_by_index(i, &tbl);
	
	if (status)
		return -ENODEV;

	return 0;
}
