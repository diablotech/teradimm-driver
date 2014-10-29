/* Includes to make the massive chunck of code to work. */
#include <linux/nls.h>
#include <acpi/acconfig.h>
#include <acpi/platform/aclinux.h>
#include <acpi/acmacros.h>
#include <acpi/actypes.h>
#include <acpi/actbl.h>
#include <acpi/actbl2.h>
#include <acpi/aclocal.h>
#include <acpi/acexcep.h>
#include <acpi/acpixf.h>
#ifndef ACPI_FREE
#define ACPI_FREE(a)        ACPI_MEM_FREE(a)
extern void acpi_os_free(void *ptr);
#endif


extern void acpi_os_printf(const char *fmt, ...);
ACPI_MODULE_NAME("nsxfeval")

void acpi_os_vprintf(const char *fmt, va_list args)
{
	static char buffer[512];

	vsprintf(buffer, fmt, args);

#ifdef ENABLE_DEBUGGER
	if (acpi_in_debugger) {
		kdb_printf("%s", buffer);
	} else {
		printk("%s", buffer);
	}
#else
	printk("%s", buffer);
#endif
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_error, acpi_ut_warning, acpi_ut_info
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              Format              - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print message with module/line/version info
 *
 ******************************************************************************/

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_error(char *module_name, u32 line_number, char *format, ...)
{
	va_list args;

	acpi_os_printf("ACPI Error (%s-%04d): ", module_name, line_number);

	va_start(args, format);
	acpi_os_vprintf(format, args);
	acpi_os_printf(" [%X]\n", ACPI_CA_VERSION);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_type_name
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a Type ID into a name string (Debug only)
 *
 ******************************************************************************/

/*
 * Elements of acpi_gbl_ns_type_names below must match
 * one-to-one with values of acpi_object_type
 *
 * The type ACPI_TYPE_ANY (Untyped) is used as a "don't care" when searching;
 * when stored in a table it really means that we have thus far seen no
 * evidence to indicate what type is actually going to be stored for this entry.
 */
static const char acpi_gbl_bad_type[] = "UNDEFINED";

/* Printable names of the ACPI object types */

static const char *acpi_gbl_ns_type_names[] = {
	/* 00 */ "Untyped",
	/* 01 */ "Integer",
	/* 02 */ "String",
	/* 03 */ "Buffer",
	/* 04 */ "Package",
	/* 05 */ "FieldUnit",
	/* 06 */ "Device",
	/* 07 */ "Event",
	/* 08 */ "Method",
	/* 09 */ "Mutex",
	/* 10 */ "Region",
	/* 11 */ "Power",
	/* 12 */ "Processor",
	/* 13 */ "Thermal",
	/* 14 */ "BufferField",
	/* 15 */ "DdbHandle",
	/* 16 */ "DebugObject",
	/* 17 */ "RegionField",
	/* 18 */ "BankField",
	/* 19 */ "IndexField",
	/* 20 */ "Reference",
	/* 21 */ "Alias",
	/* 22 */ "MethodAlias",
	/* 23 */ "Notify",
	/* 24 */ "AddrHandler",
	/* 25 */ "ResourceDesc",
	/* 26 */ "ResourceFld",
	/* 27 */ "Scope",
	/* 28 */ "Extra",
	/* 29 */ "Data",
	/* 30 */ "Invalid"
};

char *acpi_ut_get_type_name(acpi_object_type type)
{

	if (type > ACPI_TYPE_INVALID) {
		return (ACPI_CAST_PTR(char, acpi_gbl_bad_type));
	}

	return (ACPI_CAST_PTR(char, acpi_gbl_ns_type_names[type]));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_evaluate_object_typed
 *
 * PARAMETERS:  Handle              - Object handle (optional)
 *              Pathname            - Object pathname (optional)
 *              external_params     - List of parameters to pass to method,
 *                                    terminated by NULL.  May be NULL
 *                                    if no parameters are being passed.
 *              return_buffer       - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *              return_type         - Expected type of return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find and evaluate the given object, passing the given
 *              parameters if necessary.  One of "Handle" or "Pathname" must
 *              be valid (non-null)
 *
 ******************************************************************************/
acpi_status
acpi_evaluate_object_typed(acpi_handle handle,
			   acpi_string pathname,
			   struct acpi_object_list * external_params,
			   struct acpi_buffer * return_buffer,
			   acpi_object_type return_type)
{
	acpi_status status;
	u8 must_free = FALSE;

	ACPI_FUNCTION_TRACE(acpi_evaluate_object_typed);

	/* Return buffer must be valid */

	if (!return_buffer) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (return_buffer->length == ACPI_ALLOCATE_BUFFER) {
		must_free = TRUE;
	}

	/* Evaluate the object */

	status =
	    acpi_evaluate_object(handle, pathname, external_params,
				 return_buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Type ANY means "don't care" */

	if (return_type == ACPI_TYPE_ANY) {
		return_ACPI_STATUS(AE_OK);
	}

	if (return_buffer->length == 0) {

		/* Error because caller specifically asked for a return value */

		ACPI_ERROR((AE_INFO, "No return value"));
		return_ACPI_STATUS(AE_NULL_OBJECT);
	}

	/* Examine the object type returned from evaluate_object */

	if (((union acpi_object *)return_buffer->pointer)->type == return_type) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Return object type does not match requested type */

	ACPI_ERROR((AE_INFO,
		    "Incorrect return type [%s] requested [%s]",
		    acpi_ut_get_type_name(((union acpi_object *)return_buffer->
					   pointer)->type),
		    acpi_ut_get_type_name(return_type)));

	if (must_free) {

		/* Caller used ACPI_ALLOCATE_BUFFER, free the return buffer */

		ACPI_FREE(return_buffer->pointer);
		return_buffer->pointer = NULL;
	}

	return_buffer->length = 0;
	return_ACPI_STATUS(AE_TYPE);
}


/*
 * Sample implementation from Unicode home page.
 * http://www.stonehand.com/unicode/standard/fss-utf.html
 */
struct utf8_table {
	int     cmask;
	int     cval;
	int     shift;
	long    lmask;
	long    lval;
};

static const struct utf8_table utf8_table[] =
{
    {0x80,  0x00,   0*6,    0x7F,           0,         /* 1 byte sequence */},
    {0xE0,  0xC0,   1*6,    0x7FF,          0x80,      /* 2 byte sequence */},
    {0xF0,  0xE0,   2*6,    0xFFFF,         0x800,     /* 3 byte sequence */},
    {0xF8,  0xF0,   3*6,    0x1FFFFF,       0x10000,   /* 4 byte sequence */},
    {0xFC,  0xF8,   4*6,    0x3FFFFFF,      0x200000,  /* 5 byte sequence */},
    {0xFE,  0xFC,   5*6,    0x7FFFFFFF,     0x4000000, /* 6 byte sequence */},
    {0,						       /* end of table    */}
};

#define UNICODE_MAX	0x0010ffff
#define PLANE_SIZE      0x00010000

#define SURROGATE_MASK  0xfffff800
#define SURROGATE_PAIR  0x0000d800
#define SURROGATE_LOW   0x00000400
#define SURROGATE_BITS  0x000003ff

static inline unsigned long get_utf16(unsigned c, enum utf16_endian endian)
{
	switch (endian) {
	default:
		return c;
	case UTF16_LITTLE_ENDIAN:
		return __le16_to_cpu(c);
	case UTF16_BIG_ENDIAN:
		return __be16_to_cpu(c);
	}
}
typedef u32 unicode_t;
static int utf32_to_utf8(unicode_t u, u8 *s, int maxout)
{
	unsigned long l;
	int c, nc;
	const struct utf8_table *t;

	if (!s)
		return 0;

	l = u;
	if (l > UNICODE_MAX || (l & SURROGATE_MASK) == SURROGATE_PAIR)
		return -1;

	nc = 0;
	for (t = utf8_table; t->cmask && maxout; t++, maxout--) {
		nc++;
		if (l <= t->lmask) {
			c = t->shift;
			*s = (u8) (t->cval | (l >> c));
			while (c > 0) {
				c -= 6;
				s++;
				*s = (u8) (0x80 | ((l >> c) & 0x3F));
			}
			return nc;
		}
	}
	return -1;
}

int utf16s_to_utf8s(const wchar_t *pwcs, int inlen, enum utf16_endian endian,
		u8 *s, int maxout)
{
	u8 *op;
	int size;
	unsigned long u, v;

	op = s;
	while (inlen > 0 && maxout > 0) {
		u = get_utf16(*pwcs, endian);
		if (!u)
			break;
		pwcs++;
		inlen--;
		if (u > 0x7f) {
			if ((u & SURROGATE_MASK) == SURROGATE_PAIR) {
				if (u & SURROGATE_LOW) {
					/* Ignore character and move on */
					continue;
				}
				if (inlen <= 0)
					break;
				v = get_utf16(*pwcs, endian);
				if ((v & SURROGATE_MASK) != SURROGATE_PAIR ||
						!(v & SURROGATE_LOW)) {
					/* Ignore character and move on */
					continue;
				}
				u = PLANE_SIZE + ((u & SURROGATE_BITS) << 10)
						+ (v & SURROGATE_BITS);
				pwcs++;
				inlen--;
			}
			size = utf32_to_utf8(u, op, maxout);
			if (size == -1) {
				/* Ignore character and move on */
			} else {
				op += size;
				maxout -= size;
			}
		} else {
			*op++ = (u8) u;
			maxout--;
		}
	}
	return op - s;
}

