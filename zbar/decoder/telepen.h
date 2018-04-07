/*------------------------------------------------------------------------
 *  Copyright 2017 (c) Chong Hor Kong <horkongchk@gmail.com>
 *------------------------------------------------------------------------*/

#ifndef _TELEPEN_H_
#define _TELEPEN_H_

/* Telepen specific decode state */
typedef struct telepen_decoder_s {
    unsigned direction : 1;     /* scan direction: 0=fwd/bar, 1=rev/space */
	unsigned element : 5;       /* element offset 0-16 */
    int character : 12;         /* character position in symbol */
	unsigned char bufchar;		/* character encoding buffer */
	unsigned buf01 : 2;			/* narrow-wide combination buffer */
	unsigned bits;       		/* current bits buffered */
	unsigned sn;				/* current character width */
    unsigned width;             /* last character width */

    unsigned config;
    int configs[NUM_CFGS];      /* int valued configurations */
} telepen_decoder_t;

/* reset Telepen specific state */
static inline void telepen_reset (telepen_decoder_t *dtelepen)
{
    dtelepen->direction = 0;
	dtelepen->element = 0;
    dtelepen->character = -1;
	dtelepen->bufchar = 0;
	dtelepen->buf01 = 0;
	dtelepen->bits = 0;
	dtelepen->sn = 0;
}

/* decode Telepen symbols */
zbar_symbol_type_t _zbar_decode_telepen(zbar_decoder_t *dcode);

#endif
