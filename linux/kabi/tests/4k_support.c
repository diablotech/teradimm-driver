#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/version.h>

#if defined (LINUX_VERSION_CODE)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
#error
#endif


#endif
