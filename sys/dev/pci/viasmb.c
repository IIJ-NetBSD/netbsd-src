/* $NetBSD$ */
/* $FreeBSD: src/sys/pci/viapm.c,v 1.10 2005/05/29 04:42:29 nyan Exp $ */

/*-
 * Copyright (c) 2005, 2006 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions, and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2001 Alcove - Nicolas Souchu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
 
 #include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/viadevbusvar.h>
#include <dev/pci/viasmbpcireg.h>

#include <dev/i2c/i2cvar.h>
#include <dev/smbus/viasmbreg.h>

#ifdef	VIASMB_DEBUG
#define	DPRINTF(x)	printf x
#else
#define	DPRINTF(x)
#endif

#define		viasmb_smbus_read(sc, o) \
	bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, (o))
#define		viasmb_smbus_write(sc, o, v) \
	bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, (o), (v))

#define	VIASMB_SMBUS_TIMEOUT	10000
/* XXX Should be moved to smbus layer */
#define	SMB_MAXBLOCKSIZE	32

struct viasmb_softc {
	bus_space_tag_t	sc_iot;
	bus_space_handle_t sc_ioh;
	struct i2c_controller sc_i2c;

	int sc_revision;
	int sc_io_base;
};

static int	viasmb_match(device_t, cfdata_t, void *);
static void	viasmb_attach(device_t, device_t, void *);
static int	viasmb_clear(struct viasmb_softc *);
static int	viasmb_busy(struct viasmb_softc *);
/* SMBus operations */
static int      viasmb_smbus_quick_write(void *, i2c_addr_t);
static int      viasmb_smbus_quick_read(void *, i2c_addr_t);
static int      viasmb_smbus_send_byte(void *, i2c_addr_t, uint8_t);
static int      viasmb_smbus_receive_byte(void *, i2c_addr_t,
                                           uint8_t *);
static int      viasmb_smbus_read_byte(void *, i2c_addr_t, uint8_t,
					int8_t *);
static int      viasmb_smbus_write_byte(void *, i2c_addr_t, uint8_t,
					 int8_t);
static int      viasmb_smbus_read_word(void *, i2c_addr_t, uint8_t,
					int16_t *);
static int      viasmb_smbus_write_word(void *, i2c_addr_t, uint8_t,
					 int16_t);
static int      viasmb_smbus_block_write(void *, i2c_addr_t, uint8_t,
					  int, void *);
static int      viasmb_smbus_block_read(void *, i2c_addr_t, uint8_t,
					 int, void *);

static int		viasmb_exec(void *, i2c_op_t, i2c_addr_t, const void *,
			     size_t, void *, size_t, int);

CFATTACH_DECL_NEW(viasmb, sizeof(struct viasmb_softc),
    viasmb_match, viasmb_attach, NULL, NULL);
	
static int
viasmb_match(device_t parent, cfdata_t match, void *aux)
{
	struct viadevbus_attach_args *vaa = aux;
	return strcmp(vaa->vdb_name, "viasmb") == 0;
}

static void
viasmb_attach(device_t parent, device_t self, void *aux)
{
	struct viasmb_softc *sc = device_private(self);
	struct viadevbus_attach_args *vaa = aux;
	struct pci_attach_args *pa = &vaa->pa;
	pcireg_t addr, val;
	uint8_t b;

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_VIATECH_VT82C596B_PWR:
		/* FALLTHROUGH */
	case PCI_PRODUCT_VIATECH_VT82C596B_PWR_2:
		/* FALLTHROUGH */
	case PCI_PRODUCT_VIATECH_VT82C686A_PWR:
		/* FALLTHROUGH */
	case PCI_PRODUCT_VIATECH_VT8231_PWR:
		sc->sc_io_base = SMB_BASE1;
		break;
	default:
		sc->sc_io_base = SMB_BASE3;
		break;
	}


	sc->sc_iot = pa->pa_iot;
	addr = pci_conf_read(pa->pa_pc, pa->pa_tag, sc->sc_io_base);
	addr &= 0xfff0;
	aprint_naive("\n");
	aprint_normal("\n");
	if (bus_space_map(sc->sc_iot, addr, 8, 0, &sc->sc_ioh)) {
		aprint_normal(": failed to map SMBus I/O space\n");
		return;
	}

	val = pci_conf_read(pa->pa_pc, pa->pa_tag, SMB_HOST_CONFIG);
	if ((val & 0x10000) == 0) {
		aprint_normal(": SMBus is disabled\n");
		/* XXX We can enable the SMBus here by writing 1 to
		 * SMB_HOST_CONFIG, but do we want to?
		 */
		return;
	}

	switch (val & 0x0e) {
	case 8:
		DPRINTF((": interrupting at irq 9\n"));
		break;
	case 0:
		DPRINTF((": interrupting at SMI#\n"));
		break;
	default:
		DPRINTF((": interrupt misconfigured\n"));
		break;
	}

	val = pci_conf_read(pa->pa_pc, pa->pa_tag, SMB_REVISION);
	sc->sc_revision = val >> 16;
	
	aprint_normal_dev(self, "SMBus found at 0x%x (revision 0x%x)\n", addr,
	    sc->sc_revision);
	
	/* Disable slave function */
	b = viasmb_smbus_read(sc, SMBSLVCNT);
	viasmb_smbus_write(sc, SMBSLVCNT, b & ~1);

	iic_tag_init(&sc->sc_i2c);
	sc->sc_i2c.ic_cookie = (void *)sc;
	sc->sc_i2c.ic_exec = viasmb_exec;

	iicbus_attach(self, &sc->sc_i2c);
}

static int
viasmb_wait(struct viasmb_softc *sc)
{
	int rv, timeout;
	uint8_t val = 0;

	timeout = VIASMB_SMBUS_TIMEOUT;
	rv = 0;

	while (timeout--) {
		val = viasmb_smbus_read(sc, SMBHSTSTS);
		if (!(val & SMBHSTSTS_BUSY) && (val & SMBHSTSTS_INTR))
			break;
		DELAY(10);
	}

	if (timeout == 0)
		rv = EBUSY;

	if ((val & SMBHSTSTS_FAILED) || (val & SMBHSTSTS_COLLISION) ||
	    (val & SMBHSTSTS_ERROR))
		rv = EIO;

	viasmb_clear(sc);

	return rv;
}

static int
viasmb_clear(struct viasmb_softc *sc)
{
	viasmb_smbus_write(sc, SMBHSTSTS,
	    (SMBHSTSTS_FAILED | SMBHSTSTS_COLLISION | SMBHSTSTS_ERROR |
	     SMBHSTSTS_INTR));
	DELAY(10);

	return 0;
}

static int
viasmb_busy(struct viasmb_softc *sc)
{
	uint8_t val;

	val = viasmb_smbus_read(sc, SMBHSTSTS);

	return (val & SMBHSTSTS_BUSY);
}

static int
viasmb_exec(void *opaque, i2c_op_t op, i2c_addr_t addr, const void *vcmd,
    size_t cmdlen, void *vbuf, size_t buflen, int flags)
{
	struct viasmb_softc *sc;
	uint8_t cmd;
	int rv = -1;

	DPRINTF(("viasmb_exec(%p, 0x%x, 0x%x, %p, %d, %p, %d, 0x%x)\n",
	    opaque, op, addr, vcmd, cmdlen, vbuf, buflen, flags));

	sc = (struct viasmb_softc *)opaque;

	if (op != I2C_OP_READ_WITH_STOP &&
	    op != I2C_OP_WRITE_WITH_STOP)
		return -1;

	if (cmdlen > 0)
		cmd = *(uint8_t *)(__UNCONST(vcmd)); /* XXX */

	switch (cmdlen) {
	case 0:
		switch (buflen) {
		case 0:
			/* SMBus quick read/write */
			if (I2C_OP_READ_P(op))
				rv = viasmb_smbus_quick_read(sc, addr);
			else
				rv = viasmb_smbus_quick_write(sc, addr);

			return rv;
		case 1:
			/* SMBus send/receive byte */
			if (I2C_OP_READ_P(op))
				rv = viasmb_smbus_send_byte(sc, addr,
				    *(uint8_t *)vbuf);
			else
				rv = viasmb_smbus_receive_byte(sc, addr,
				    (uint8_t *)vbuf);
			return rv;
		default:
			return -1;
		}
	case 1:
		switch (buflen) {
		case 0:
			return -1;
		case 1:
			/* SMBus read/write byte */
			if (I2C_OP_READ_P(op))
				rv = viasmb_smbus_read_byte(sc, addr,
				    cmd, (uint8_t *)vbuf);
			else
				rv = viasmb_smbus_write_byte(sc, addr,
				    cmd, *(uint8_t *)vbuf);
			return rv;
		case 2:
			/* SMBus read/write word */
			if (I2C_OP_READ_P(op))
				rv = viasmb_smbus_read_word(sc, addr,
				    cmd, (uint16_t *)vbuf);
			else
				rv = viasmb_smbus_write_word(sc, addr,
				    cmd, *(uint16_t *)vbuf);
			return rv;
		default:
			/* SMBus read/write block */
			if (I2C_OP_READ_P(op))
				rv = viasmb_smbus_block_read(sc, addr,
				    cmd, buflen, vbuf);
			else
				rv = viasmb_smbus_block_write(sc, addr,
				    cmd, buflen, vbuf);
			return rv;
		}
	}

	return -1; /* XXX ??? */
}

static int
viasmb_smbus_quick_write(void *opaque, i2c_addr_t slave)
{
	struct viasmb_softc *sc;

	sc = (struct viasmb_softc *)opaque;

	DPRINTF(("viasmb_smbus_quick_write(%p, 0x%x)\n", opaque, slave));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	viasmb_smbus_write(sc, SMBHSTADD, (slave & 0x7f) << 1);
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_QUICK);
	if (viasmb_wait(sc))
		return EBUSY;
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_QUICK | SMBHSTCNT_START);

	return viasmb_wait(sc);
}

static int
viasmb_smbus_quick_read(void *opaque, i2c_addr_t slave)
{
	struct viasmb_softc *sc;

	sc = (struct viasmb_softc *)opaque;

	DPRINTF(("viasmb_smbus_quick_read(%p, 0x%x)\n", opaque, slave));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	viasmb_smbus_write(sc, SMBHSTADD, ((slave & 0x7f) << 1) | 1);
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_QUICK);
	if (viasmb_wait(sc))
		return EBUSY;
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_QUICK | SMBHSTCNT_START);

	return viasmb_wait(sc);
}

static int
viasmb_smbus_send_byte(void *opaque, i2c_addr_t slave, uint8_t byte)
{
	struct viasmb_softc *sc;

	sc = (struct viasmb_softc *)opaque;

	DPRINTF(("viasmb_smbus_send_byte(%p, 0x%x, 0x%x)\n", opaque,
	    slave, byte));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	viasmb_smbus_write(sc, SMBHSTADD, (slave & 0x7f) << 1);
	viasmb_smbus_write(sc, SMBHSTCMD, byte);
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_SENDRECV);
	if (viasmb_wait(sc))
		return EBUSY;
	viasmb_smbus_write(sc, SMBHSTCNT,
	    SMBHSTCNT_START | SMBHSTCNT_SENDRECV);

	return viasmb_wait(sc);
}

static int
viasmb_smbus_receive_byte(void *opaque, i2c_addr_t slave, uint8_t *pbyte)
{
	struct viasmb_softc *sc;
	int rv;

	sc = (struct viasmb_softc *)opaque;

	DPRINTF(("viasmb_smbus_receive_byte(%p, 0x%x, %p)\n", opaque,
	    slave, pbyte));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	viasmb_smbus_write(sc, SMBHSTADD, ((slave & 0x7f) << 1) | 1);
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_SENDRECV);
	if (viasmb_wait(sc))
		return EBUSY;
	viasmb_smbus_write(sc, SMBHSTCNT,
	    SMBHSTCNT_START | SMBHSTCNT_SENDRECV);

	rv = viasmb_wait(sc);
	if (rv == 0)
		*pbyte = viasmb_smbus_read(sc, SMBHSTDAT0);

	return rv;
}

static int
viasmb_smbus_write_byte(void *opaque, i2c_addr_t slave, uint8_t cmd,
		   int8_t byte)
{
	struct viasmb_softc *sc;

	sc = (struct viasmb_softc *)opaque;

	DPRINTF(("viasmb_smbus_write_byte(%p, 0x%x, 0x%x, 0x%x)\n", opaque,
	    slave, cmd, byte));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	viasmb_smbus_write(sc, SMBHSTADD, (slave & 0x7f) << 1);
	viasmb_smbus_write(sc, SMBHSTCMD, cmd);
	viasmb_smbus_write(sc, SMBHSTDAT0, byte);
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_BYTE);
	if (viasmb_wait(sc))
		return EBUSY;
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_START | SMBHSTCNT_BYTE);

	return viasmb_wait(sc);
}

static int
viasmb_smbus_read_byte(void *opaque, i2c_addr_t slave, uint8_t cmd,
		  int8_t *pbyte)
{
	struct viasmb_softc *sc;
	int rv;

	sc = (struct viasmb_softc *)opaque;

	DPRINTF(("viasmb_smbus_read_byte(%p, 0x%x, 0x%x, %p)\n", opaque,
	    slave, cmd, pbyte));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	viasmb_smbus_write(sc, SMBHSTADD, ((slave & 0x7f) << 1) | 1);
	viasmb_smbus_write(sc, SMBHSTCMD, cmd);
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_BYTE);
	if (viasmb_wait(sc))
		return EBUSY;
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_START | SMBHSTCNT_BYTE);
	rv = viasmb_wait(sc);
	if (rv == 0)
		*pbyte = viasmb_smbus_read(sc, SMBHSTDAT0);

	return rv;
}

static int
viasmb_smbus_write_word(void *opaque, i2c_addr_t slave, uint8_t cmd,
		   int16_t word)
{
	struct viasmb_softc *sc;

	sc = (struct viasmb_softc *)opaque;

	DPRINTF(("viasmb_smbus_write_word(%p, 0x%x, 0x%x, 0x%x)\n", opaque,
	    slave, cmd, word));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	viasmb_smbus_write(sc, SMBHSTADD, (slave & 0x7f) << 1);
	viasmb_smbus_write(sc, SMBHSTCMD, cmd);
	viasmb_smbus_write(sc, SMBHSTDAT0, word & 0x00ff);
	viasmb_smbus_write(sc, SMBHSTDAT1, (word & 0xff00) >> 8);
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_WORD);
	if (viasmb_wait(sc))
		return EBUSY;
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_START | SMBHSTCNT_WORD);

	return viasmb_wait(sc);
}

static int
viasmb_smbus_read_word(void *opaque, i2c_addr_t slave, uint8_t cmd,
		  int16_t *pword)
{
	struct viasmb_softc *sc;
	int rv;
	int8_t high, low;

	sc = (struct viasmb_softc *)opaque;

	DPRINTF(("viasmb_smbus_read_word(%p, 0x%x, 0x%x, %p)\n", opaque,
	    slave, cmd, pword));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	viasmb_smbus_write(sc, SMBHSTADD, ((slave & 0x7f) << 1) | 1);
	viasmb_smbus_write(sc, SMBHSTCMD, cmd);
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_WORD);
	if (viasmb_wait(sc))
		return EBUSY;
	viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_START | SMBHSTCNT_WORD);

	rv = viasmb_wait(sc);
	if (rv == 0) {
		low = viasmb_smbus_read(sc, SMBHSTDAT0);
		high = viasmb_smbus_read(sc, SMBHSTDAT1);
		*pword = ((high & 0xff) << 8) | (low & 0xff);
	}

	return rv;
}

static int
viasmb_smbus_block_write(void *opaque, i2c_addr_t slave, uint8_t cmd,
		    int cnt, void *data)
{
	struct viasmb_softc *sc;
	int8_t *buf;
	int remain, len, i;
	int rv;

	sc = (struct viasmb_softc *)opaque;
	buf = (int8_t *)data;
	rv = 0;

	DPRINTF(("viasmb_smbus_block_write(%p, 0x%x, 0x%x, %d, %p)\n",
	    opaque, slave, cmd, cnt, data));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	remain = cnt;
	while (remain) {
		len = uimin(remain, SMB_MAXBLOCKSIZE);

		viasmb_smbus_write(sc, SMBHSTADD, (slave & 0x7f) << 1);
		viasmb_smbus_write(sc, SMBHSTCMD, cmd);
		viasmb_smbus_write(sc, SMBHSTDAT0, len);
		i = viasmb_smbus_read(sc, SMBHSTCNT);
		/* XXX FreeBSD does this, but it looks wrong */
		for (i = 0; i < len; i++) {
			viasmb_smbus_write(sc, SMBBLKDAT,
			    buf[cnt - remain + i]);
		}
		viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_BLOCK);
		if (viasmb_wait(sc))
			return EBUSY;
		viasmb_smbus_write(sc, SMBHSTCNT,
		    SMBHSTCNT_START | SMBHSTCNT_BLOCK);
		if (viasmb_wait(sc))
			return EBUSY;

		remain -= len;
	}

	return rv;
}

static int
viasmb_smbus_block_read(void *opaque, i2c_addr_t slave, uint8_t cmd,
			 int cnt, void *data)
{
	struct viasmb_softc *sc;
	int8_t *buf;
	int remain, len, i;
	int rv;

	sc = (struct viasmb_softc *)opaque;
	buf = (int8_t *)data;
	rv = 0;

	DPRINTF(("viasmb_smbus_block_read(%p, 0x%x, 0x%x, %d, %p)\n",
	    opaque, slave, cmd, cnt, data));

	viasmb_clear(sc);
	if (viasmb_busy(sc))
		return EBUSY;

	remain = cnt;
	while (remain) {
		viasmb_smbus_write(sc, SMBHSTADD, ((slave & 0x7f) << 1) | 1);
		viasmb_smbus_write(sc, SMBHSTCMD, cmd);
		viasmb_smbus_write(sc, SMBHSTCNT, SMBHSTCNT_BLOCK);
		if (viasmb_wait(sc))
			return EBUSY;
		viasmb_smbus_write(sc, SMBHSTCNT,
		    SMBHSTCNT_START | SMBHSTCNT_BLOCK);
		if (viasmb_wait(sc))
			return EBUSY;

		len = viasmb_smbus_read(sc, SMBHSTDAT0);
		i = viasmb_smbus_read(sc, SMBHSTCNT);

		len = uimin(len, remain);

		/* FreeBSD does this too... */
		for (i = 0; i < len; i++) {
			buf[cnt - remain + i] =
			    viasmb_smbus_read(sc, SMBBLKDAT);
		}

		remain -= len;
	}

	return rv;
}
