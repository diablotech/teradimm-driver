#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/acpi.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <acpi/actypes.h>

void foo(void *handle)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_evaluate_object_typed(handle, "_STR", NULL, &buffer, ACPI_TYPE_INTEGER);
}
