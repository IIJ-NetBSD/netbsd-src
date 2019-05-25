#include "opt_soc.h"
#include "opt_multiprocessor.h"
#include "opt_console.h"

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: imx8_platform.c $");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/termios.h>

#include <dev/fdt/fdtvar.h>
#include <arm/fdt/arm_fdtvar.h>

#include <uvm/uvm_extern.h>

#include <machine/bootconfig.h>
#include <arm/cpufunc.h>

#include <arm/cpufunc.h>

#include <arm/cortex/gtmr_var.h>
#include <arm/cortex/gic_reg.h>

#include <dev/ic/ns16550reg.h> // verify before adding
#include <dev/ic/comreg.h>

#include <arm/arm/psci.h>
#include <arm/fdt/psci_fdtvar.h>

#include <libfdt.h>
#include <arm/nxp/imx8_platform.h>


extern struct arm32_bus_dma_tag arm_generic_dma_tag;
extern struct bus_space arm_generic_bs_tag;
extern struct bus_space arm_generic_a4x_bs_tag;


void imx8_platform_early_putchar(char);

void
imx8_platform_early_putchar(char c)
{
#ifdef CONSADDR
#define CONSADDR_VA	((CONSADDR - IMX8_CORE_PBASE) + IMX8_CORE_VBASE)
	volatile uint32_t *uartaddr = cpu_earlydevice_va_p() ?
		(volatile uint32_t *)CONSADDR_VA :
		(volatile uint32_t *)CONSADDR;
	while ((le32toh(uartaddr[com_lsr]) & LSR_TXRDY) == 0)
		;
	uartaddr[com_data] = htole32(c);
#endif
}


static const struct pmap_devmap *
imx8_platform_devmap(void)
{
	static const struct pmap_devmap devmap[] = {
		DEVMAP_ENTRY(IMX8_CORE_VBASE,
			     IMX8_CORE_PBASE,
			     IMX8_CORE_SIZE),
		DEVMAP_ENTRY_END
	};

	return devmap;
}

static void
imx8_platform_init_attach_args(struct fdt_attach_args *faa)
{
	faa->faa_bst = &arm_generic_bs_tag;	// add these
	faa->faa_a4x_bst = &arm_generic_a4x_bs_tag;
	faa->faa_dmat = &arm_generic_dma_tag;
}


static void
imx8_platform_device_register(device_t self, void *aux)
{
}

static u_int
imx8_platform_uart_freq(void)
{
	return IMX8_BASE_FREQ;   // add the value to this macro
}


static const struct arm_platform imx8m_platform = {
	.ap_devmap = imx8_platform_devmap,  //write the function
	.ap_bootstrap = arm_fdt_cpu_bootstrap,  //imx8_platform_bootstrap
	.ap_init_attach_args = imx8_platform_init_attach_args,
	.ap_device_register = imx8_platform_device_register,  //empty function for now
	.ap_reset = psci_fdt_reset,  //imx8_platform_reset
	.ap_delay = gtmr_delay,
	.ap_uart_freq = imx8_platform_uart_freq,
	.ap_mpstart = arm_fdt_cpu_mpstart,
};

ARM_PLATFORM(imx8, "nxp,imx8m", &imx8m_platform);
