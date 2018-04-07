/*------------------------------------------------------------------------
 *  Copyright 2017 (c) Chong Hor Kong <horkongchk@gmail.com>
 *------------------------------------------------------------------------*/

#include <config.h>
#include <zbar.h>
#include <stdio.h>
//#define DEBUG_TELEPEN 1
#ifdef DEBUG_TELEPEN
# define DEBUG_LEVEL (DEBUG_TELEPEN)
#endif
#include "debug.h"
#include "decoder.h"

#define NUM_CHARS (0x2c)

typedef struct telepen_s {
    unsigned bits;
	signed char rev, fwd;
} telepen_t;

static const telepen_t telepen_encodings[4] = {
    { 0x1, 0x1, 0x1 },	/* 00 - 1 */
    { 0x2, 0x0, 0xff },	/* 01 - 00/0110 */
    { 0x2, 0xff, 0x0 },	/* 02 - 0110/00 */
    { 0x3, 0x2, 0x2 },	/* 03 - 010 */
};

static unsigned char reverse_lookup[16] = {
0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf, };

static inline unsigned char reverse(unsigned char n) {
   // Reverse the top and bottom nibble then swap them.
   return (reverse_lookup[n&0b1111] << 4) | reverse_lookup[n>>4];
}

static inline unsigned char check_parity(unsigned char x)
{
unsigned char data;
/* raise flag if odd */
data = x ^(x >> 1);
data ^= (data >> 2);
data ^= (data >> 4);
return (data & 1);
}

static inline int check_width (unsigned cur, unsigned prev)
{
    unsigned dw;
    if(prev > cur)
        dw = prev - cur;
    else
        dw = cur - prev;
    dw *= 4;
    return(dw > prev);
}
	
static inline unsigned char telepen_decode1 (unsigned char enc,
                                            unsigned e,
                                            unsigned s)
{
	/*
	> 4.7/16 - 0xff
	> 1.7/16 - 0x3
	< 1.7/16 - 0x1
	< 0.2/16 - 0xff
	*/
    unsigned char E = decode_e(e, s, 128);
    if(E > 36)
        return(0xff);
    enc <<= 1;
    if(E > 12)
	{
        enc |= 1;
        dbprintf(2, "1");
    }
    else {
        dbprintf(2, "0");
	}
    return(enc);
}

static inline signed char telepen_decode2 (zbar_decoder_t *dcode)
{
	telepen_decoder_t *dtelepen = &dcode->telepen;

	/* read previous element */
	unsigned enc = 0, w1 = get_width(dcode, 1);
	enc = telepen_decode1(enc,
							w1,
							dtelepen->width);
	if(enc == 0xff)
        return(-1);
	
	/* read current element */
	unsigned w0 = get_width(dcode, 0);
	enc = telepen_decode1(enc,
							w0,
							dtelepen->width);
	if(enc == 0xff)
		if(dtelepen->bufchar == 0x2f &&	// 0b0101111 - last blank is hidden
			get_color(dcode) != ZBAR_BAR)
			enc = 0;					// double narrow encoding
		else
			return(-1);
	
	const telepen_t *c = &telepen_encodings[enc];
	
	dtelepen->sn = dtelepen->sn + w0 + w1;
	dtelepen->bits += c->bits;
	
	unsigned char dec = ((dtelepen->direction) ? c->rev : c->fwd);
	dbprintf(2, " i=%d chk=%d c=%d buf=%d", c->bits, dtelepen->bits, dec, dtelepen->buf01);
	
	if (dec == 0xff)
		if (dtelepen->buf01 == 2)		// 3rd encounter
			return(-1);
		else if (dtelepen->buf01 == 1)	// 2nd encounter
		{
			dtelepen->buf01 = 2;
			dec = 0x2;					// 0b10
		}
		else							// 1st encounter
		{
			dtelepen->buf01 = 1;
			dec = 0x1;					// 0b01
		}

			
	dtelepen->bufchar = (dtelepen->bufchar << c->bits) + dec; 
	
	if (dtelepen->bits > 8)
		return(-1);
	else
		return(0);
}

static inline signed char telepen_decode_start (zbar_decoder_t *dcode)
{
    telepen_decoder_t *dtelepen = &dcode->telepen;
	unsigned e = dtelepen->element;
	
	/* e1 = quiet zone */
	/* 12 elements in the start character (1 hidden if rev)*/
	if(e < 12)
		return(ZBAR_NONE);
	else  if(e > 12)
		dtelepen->element = 0;
	
	/* 11(rev) or 12(fwd) elements in width */
    dtelepen->width = calc_s(dcode, 0, e-1);
	/* simulate hidden element and assume rev if 11 */
	if(e == 12)
		dtelepen->width += get_width(dcode, 10);
	
	if(dtelepen->width < 16)
		return(ZBAR_NONE);
	
	dbprintf(2, "\n s=%d ", dtelepen->width);
	unsigned enc = 0;
	signed char i;
	
	/* check leading quiet zone - spec is 10x */
    unsigned quiet = get_width(dcode, e-1);
    if(quiet && quiet < dtelepen->width / 2) {
        dbprintf(1, " [invalid quiet]\n");
        return(ZBAR_NONE);
    }
	
	/* first 9(rev)/10(fwd) elements should be narrow (0) */
	for(i = e - 2; i >= 2; i--)
	{
		enc = telepen_decode1(enc,
								get_width(dcode, i),
								dtelepen->width);
		if(enc > 0)
			return(ZBAR_NONE);
	}
	
	/* last two elements are wide */
	for(; i >= 0; i--)
	{
		enc = telepen_decode1(enc,
								get_width(dcode, i),
								dtelepen->width);
		if(enc == 0xff)
            return(ZBAR_NONE);				
	}
	
	if(enc != 0x3)
		return(ZBAR_NONE);

	switch(e)
	{
		case 12:dtelepen->direction = 1; //rev
		break;
		case 13:dtelepen->direction = 0; //fwd
		break;
		default: return(ZBAR_NONE);
		break;
	}
	dtelepen->element = 0;
    dtelepen->character = 0;
    dbprintf(1, " dir=%x [valid start]\n", dtelepen->direction);
    return(ZBAR_PARTIAL);
}

static inline zbar_symbol_type_t decode_abort (zbar_decoder_t *dcode,
												const char *reason)
{
    telepen_decoder_t *dtelepen = &dcode->telepen;
    if(dtelepen->character >= 1) {
        release_lock(dcode, ZBAR_TELEPEN);
		dbprintf(1, " release!");
	}
    dtelepen->character = -1;
    if(reason)
        dbprintf(1, " [%s]\n", reason);
    return(ZBAR_NONE);
}

static inline int postprocess (zbar_decoder_t *dcode)
{
    telepen_decoder_t *dtelepen = &dcode->telepen;
    dcode->direction = 1 - 2 * dtelepen->direction;
    int i, check = 0;
    if(dtelepen->direction) {
        /* reverse buffer */
        dbprintf(2, " (rev)");
        for(i = 0; i < dtelepen->character / 2; i++) {
            unsigned j = dtelepen->character - 1 - i;
            char code = dcode->buf[i];
            dcode->buf[i] = dcode->buf[j];
            dcode->buf[j] = code;
        }
    }
	/* calculate checksum */
	for(i = 0; i < dtelepen->character - 1; i++)
		check += dcode->buf[i];
	check = 127 - (check % 127);
	
	if(check != dcode->buf[dtelepen->character - 1])
		return(1); // Raise error
	
	/* trim checksum character */
    dtelepen->character--;
			
    zassert(dtelepen->character < dcode->buf_alloc, -1, "i=%02x %s\n", dtelepen->character,
            _zbar_decoder_buf_dump(dcode->buf, dtelepen->character));
    dcode->buflen = dtelepen->character;
    dcode->buf[dtelepen->character] = '\0';
    dcode->modifiers = 0;
    return(0);
}

zbar_symbol_type_t _zbar_decode_telepen (zbar_decoder_t *dcode)
{
    telepen_decoder_t *dtelepen = &dcode->telepen;
		
	/* start bit */
    if(dtelepen->character < 0) {
		dtelepen->element++;
		// triggers at element = 11(rev) or 12(fwd)
        return(telepen_decode_start(dcode));
    }
	/* post-character check */
	if(dtelepen->element == 0)
	{
		unsigned space = get_width(dcode, 0);
        if(dtelepen->character &&
           (dcode->buf[dtelepen->character - 1] == 0x7a ||	/* STOP */
		   dcode->buf[dtelepen->character - 1] == 0x5f))	/* START (rev) */
		   {
            /* trailing quiet zone check */
            if(space && space < dtelepen->width / 2)	// not end bit
                return(ZBAR_NONE);
            
			/* trim START/STOP character */
            dtelepen->character--;
			
			if(dtelepen->character < 2)			// data + checksum
                return(decode_abort(dcode, "invalid len"));
            else if(postprocess(dcode))					// checksum
				return(decode_abort(dcode, "invalid encoding"));
			/* FIXME compressed numeric */
            dbprintf(2, " [valid end]\n");
			dtelepen->character = -1;
            return(ZBAR_TELEPEN);
        }
	}
	
	/* process after every 2 element of active symbol */
    if(((++dtelepen->element) % 2) != 0)
        return(ZBAR_NONE);

    dbprintf(1, "      telepen[%c%02d+%x]",
             (dtelepen->direction) ? '<' : '>',
             dtelepen->character, dtelepen->element);

    signed char c = telepen_decode2(dcode);
    dbprintf(1, " %d", c);
    if(c < 0)
        return(decode_abort(dcode, "aborted"));
	else if(dtelepen->bits == 8)
	{
		/* character complete */
		dtelepen->buf01 = 0;
		dtelepen->bits = 0;
		dtelepen->element = 0;
		
		if(check_width(dtelepen->sn, dtelepen->width))
			return(decode_abort(dcode, "width var"));
		dtelepen->width = dtelepen->sn;
		dtelepen->sn = 0;
		
		if(size_buf(dcode, dtelepen->character + 1))
			return(decode_abort(dcode, "overflow"));
		
		/* reverse bufchar if fwd mode and check/remove parity bit */
		if(!dtelepen->direction)
			dtelepen->bufchar = reverse(dtelepen->bufchar);
		
		if(check_parity(dtelepen->bufchar))
			return(decode_abort(dcode, "parity error"));
		
		if(dtelepen->character == 0) {
			/* lock shared resources */
			if(acquire_lock(dcode, ZBAR_TELEPEN))
				return(decode_abort(dcode, NULL));
		}
		
		dcode->buf[dtelepen->character] = dtelepen->bufchar & 0x7f;
		dtelepen->character++;
	}

    dbprintf(2, "\n");
    return(ZBAR_NONE);
}