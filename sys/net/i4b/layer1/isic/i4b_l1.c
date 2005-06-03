/*
 * Copyright (c) 1997, 2001 Hellmuth Michaelis. All rights reserved.
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
 *
 *---------------------------------------------------------------------------
 *
 *	i4b_l1.c - isdn4bsd layer 1 handler
 *	-----------------------------------
 *
 * $FreeBSD: src/sys/i4b/layer1/isic/i4b_l1.c,v 1.5.2.1 2001/08/10 14:08:38 obrien Exp $
 * $DragonFly: src/sys/net/i4b/layer1/isic/i4b_l1.c,v 1.4 2005/06/03 16:50:05 dillon Exp $
 *
 *      last edit-date: [Wed Jan 24 09:12:03 2001]
 *
 *---------------------------------------------------------------------------*/

#include "use_isic.h"

#if NISIC > 0

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/thread2.h>

#include <net/if.h>

#include <net/i4b/include/machine/i4b_debug.h>
#include <net/i4b/include/machine/i4b_ioctl.h>
#include <net/i4b/include/machine/i4b_trace.h>

#include "i4b_isic.h"
#include "i4b_isac.h"

#include "../i4b_l1.h"

#include "../../include/i4b_mbuf.h"
#include "../../include/i4b_global.h"

/*---------------------------------------------------------------------------*
 *
 *	L2 -> L1: PH-DATA-REQUEST
 *	=========================
 *
 *	parms:
 *		unit		physical interface unit number
 *		m		mbuf containing L2 frame to be sent out
 *		freeflag	MBUF_FREE: free mbuf here after having sent
 *						it out
 *				MBUF_DONTFREE: mbuf is freed by Layer 2
 *	returns:
 *		==0	fail, nothing sent out
 *		!=0	ok, frame sent out
 *
 *---------------------------------------------------------------------------*/
int
isic_ph_data_req(int unit, struct mbuf *m, int freeflag)
{
	u_char cmd;
	struct l1_softc *sc = &l1_sc[unit];

#ifdef NOTDEF
	NDBGL1(L1_PRIM, "unit %d, freeflag=%d", unit, freeflag);
#endif

	if(m == NULL)			/* failsafe */
		return (0);

	crit_enter();

	if(sc->sc_I430state == ST_F3)	/* layer 1 not running ? */
	{
		NDBGL1(L1_I_ERR, "still in state F3!");
		isic_ph_activate_req(unit);
	}

	if(sc->sc_state & ISAC_TX_ACTIVE)
	{
		if(sc->sc_obuf2 == NULL)
		{
			sc->sc_obuf2 = m;		/* save mbuf ptr */

			if(freeflag)
				sc->sc_freeflag2 = 1;	/* IRQ must mfree */
			else
				sc->sc_freeflag2 = 0;	/* IRQ must not mfree */

			NDBGL1(L1_I_MSG, "using 2nd ISAC TX buffer, state = %s", isic_printstate(sc));

			if(sc->sc_trace & TRACE_D_TX)
			{
				i4b_trace_hdr_t hdr;
				hdr.unit = L0ISICUNIT(unit);
				hdr.type = TRC_CH_D;
				hdr.dir = FROM_TE;
				hdr.count = ++sc->sc_trace_dcount;
				MICROTIME(hdr.time);
				i4b_l1_trace_ind(&hdr, m->m_len, m->m_data);
			}
			crit_exit();
			return(1);
		}

		NDBGL1(L1_I_ERR, "No Space in TX FIFO, state = %s", isic_printstate(sc));
	
		if(freeflag == MBUF_FREE)
			i4b_Dfreembuf(m);			
	
		crit_exit();
		return (0);
	}

	if(sc->sc_trace & TRACE_D_TX)
	{
		i4b_trace_hdr_t hdr;
		hdr.unit = L0ISICUNIT(unit);
		hdr.type = TRC_CH_D;
		hdr.dir = FROM_TE;
		hdr.count = ++sc->sc_trace_dcount;
		MICROTIME(hdr.time);
		i4b_l1_trace_ind(&hdr, m->m_len, m->m_data);
	}
	
	sc->sc_state |= ISAC_TX_ACTIVE;	/* set transmitter busy flag */

	NDBGL1(L1_I_MSG, "ISAC_TX_ACTIVE set");

	sc->sc_freeflag = 0;		/* IRQ must NOT mfree */
	
	ISAC_WRFIFO(m->m_data, min(m->m_len, ISAC_FIFO_LEN)); /* output to TX fifo */

	if(m->m_len > ISAC_FIFO_LEN)	/* message > 32 bytes ? */
	{
		sc->sc_obuf = m;	/* save mbuf ptr */
		sc->sc_op = m->m_data + ISAC_FIFO_LEN; 	/* ptr for irq hdl */
		sc->sc_ol = m->m_len - ISAC_FIFO_LEN;	/* length for irq hdl */

		if(freeflag)
			sc->sc_freeflag = 1;	/* IRQ must mfree */
		
		cmd = ISAC_CMDR_XTF;
	}
	else
	{
		sc->sc_obuf = NULL;
		sc->sc_op = NULL;
		sc->sc_ol = 0;

		if(freeflag)
			i4b_Dfreembuf(m);

		cmd = ISAC_CMDR_XTF | ISAC_CMDR_XME;
  	}

	ISAC_WRITE(I_CMDR, cmd);
	ISACCMDRWRDELAY();

	crit_exit();
	
	return(1);
}

/*---------------------------------------------------------------------------*
 *
 *	L2 -> L1: PH-ACTIVATE-REQUEST
 *	=============================
 *
 *	parms:
 *		unit	physical interface unit number
 *
 *	returns:
 *		==0	
 *		!=0	
 *
 *---------------------------------------------------------------------------*/
int
isic_ph_activate_req(int unit)
{
	struct l1_softc *sc = &l1_sc[unit];
	NDBGL1(L1_PRIM, "unit %d", unit);
	isic_next_state(sc, EV_PHAR);
	return(0);
}

/*---------------------------------------------------------------------------*
 *	command from the upper layers
 *---------------------------------------------------------------------------*/
int
isic_mph_command_req(int unit, int command, void *parm)
{
	struct l1_softc *sc = &l1_sc[unit];

	switch(command)
	{
		case CMR_DOPEN:		/* daemon running */
			NDBGL1(L1_PRIM, "unit %d, command = CMR_DOPEN", unit);
			sc->sc_enabled = 1;			
			break;
			
		case CMR_DCLOSE:	/* daemon not running */
			NDBGL1(L1_PRIM, "unit %d, command = CMR_DCLOSE", unit);
			sc->sc_enabled = 0;
			break;

		case CMR_SETTRACE:
			NDBGL1(L1_PRIM, "unit %d, command = CMR_SETTRACE, parm = %d", unit, (unsigned int)parm);
			sc->sc_trace = (unsigned int)parm;
			break;
		
		case CMR_GCST:
			{
			struct chipstat *cst;
			NDBGL1(L1_PRIM, "unit %d, command = CMR_GCST, parm = %d", unit, (unsigned int)parm);
                        cst = (struct chipstat *)parm;
                        cst->driver_type = L1DRVR_ISIC;
			cst->stats.hscxstat.unit = sc->sc_unit;
			cst->stats.hscxstat.chan = cst->driver_bchannel;
			cst->stats.hscxstat.vfr = sc->sc_chan[cst->driver_bchannel].stat_VFR;
			cst->stats.hscxstat.rdo = sc->sc_chan[cst->driver_bchannel].stat_RDO;
			cst->stats.hscxstat.crc = sc->sc_chan[cst->driver_bchannel].stat_CRC;
			cst->stats.hscxstat.rab = sc->sc_chan[cst->driver_bchannel].stat_RAB;
			cst->stats.hscxstat.xdu = sc->sc_chan[cst->driver_bchannel].stat_XDU;
			cst->stats.hscxstat.rfo = sc->sc_chan[cst->driver_bchannel].stat_RFO;
			break;
			}
		default:
			NDBGL1(L1_ERROR, "ERROR, unknown command = %d, unit = %d, parm = %d", command, unit, (unsigned int)parm);
			break;
	}

	return(0);
}

#endif /* NISIC > 0 */
