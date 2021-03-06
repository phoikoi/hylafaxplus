/*	$Id: TagLine.c++ 1108 2012-06-26 03:56:56Z faxguy $ */
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
 * HylaFAX is a trademark of Silicon Graphics
 *
 * Permission to use, copy, modify, distribute, and sell this software and 
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 * 
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
 * 
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
 * OF THIS SOFTWARE.
 */
#include "FaxServer.h"
#include "PCFFont.h"
#include "StackBuffer.h"
#include "FaxFont.h"
#include "FaxRequest.h"
#if HAS_LOCALE
extern "C" {
#include <locale.h>
}
#endif
#if HAS_LANGINFO
extern "C" {
#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif
#include <langinfo.h>
}
#endif

#include "Sys.h"

static void
insert(fxStr& tag, u_int l, const fxStr& s)
{
    tag.remove(l,2);
    tag.insert(s, l);
}

/*
 * Read in the PCF font to use for imaging the tag line and
 * preformat as much of the tag line as possible.
 */
void
FaxModem::setupTagLine(const FaxRequest& req, const fxStr& tagLineFmt, const fxStr& locale, const fxStr& tz)
{
    if (tagLineFont == NULL)
	tagLineFont = new PCFFont;
    if (!tagLineFont->isReady() && conf.tagLineFontFile != "")
	(void) tagLineFont->read(conf.tagLineFontFile);
    tagLineLocale = locale;
#ifdef LC_CTYPE
    setlocale(LC_CTYPE, tagLineLocale);         // for <ctype.h> calls
#endif
#ifdef LC_TIME
    setlocale(LC_TIME, tagLineLocale);          // for strftime calls
#endif
    bool isutf8 = false;
#if HAS_LANGINFO
    isutf8 = (strcmp(nl_langinfo(CODESET), "UTF-8") == 0);
    tagLine.setUTF8(isutf8);
#endif

    if (tz != "") setenv("TZ", tz, 1);

    time_t t = Sys::now();
    tm* tm = localtime(&t);
    char line[1024];
    strftime(line, sizeof (line)-1, tagLineFmt, tm);
    tagLine = line;
    u_int l = 0;
    while (l < tagLine.length()) {
	l = tagLine.next(l, '%');
	if (l >= tagLine.length()-1)
	    break;
	switch (tagLine[l+1]) {
	case 'a': insert(tagLine, l, req.subaddr); break;
	case 'c': insert(tagLine, l, req.company); break;
	case 'C': insert(tagLine, l, req.fromcompany); break;
	case 'd': insert(tagLine, l, req.external); break;
	case 'g': insert(tagLine, l, req.location); break;
	case 'G': insert(tagLine, l, req.fromlocation); break;
	case 'i': insert(tagLine, l, req.jobid); break;
	case 'I': insert(tagLine, l, req.groupid); break;
	case 'j': insert(tagLine, l, req.jobtag); break;
	case 'l': insert(tagLine, l, server.getLocalIdentifier()); break;
	case 'm': insert(tagLine, l, req.mailaddr); break;
	case 'n':
	    if (req.faxnumber == "")
		insert(tagLine, l, server.getModemNumber());
	    else
		insert(tagLine, l, req.faxnumber);
	    break;
	case 'r': insert(tagLine, l, req.receiver); break;
	case 's': insert(tagLine, l, req.sender); break;
	case 'S': insert(tagLine, l, req.regarding); break;
	case 't': insert(tagLine, l,
			fxStr((int)(req.totpages-req.npages), "%u")); break;
	case 'T': insert(tagLine, l,
			fxStr((int)(req.totpages+req.skippedpages-req.nocountcover), "%u")); break;
	case 'v': insert(tagLine, l, req.voice); break;
	case 'V': insert(tagLine, l, req.fromvoice); break;
	case '%': tagLine.remove(l); break;
	default:  l += 2; break;
	}
    }
    /*
     * Break the tag into fields.
     */
    tagLineFields = 0;
    for (l = 0; l < tagLine.length(); l = tagLine.next(l+1, '|'))
	tagLineFields++;
}

#define	MARGIN_TOP	2
#define	MARGIN_BOT	2
#define	MARGIN_LEFT	2
#define	MARGIN_RIGHT	2
#define SLOP_LINES	3

/*
 * Calculate a ``slop factor'' to use in processing the tag line.
 * This is the amount of space to preallocate for storing the
 * encoded tag line once its been imaged.  We guestimate that
 * the encoded data will be <= the amount of space needed to store
 * the unencoded bitmap raster.  If this number is low then the
 * encoded raster will be truncated resulting in the tag line
 * being cropped at the bottom; probably together with a decoding
 * error of one row at the receiver.
 */
bool
FaxModem::setupTagLineSlop(const Class2Params& params)
{
    if (tagLineFont->isReady() && tagLineFields > 0) {
	if (params.jp) {
	    // color fax come to us decoded already, no slop needed 
	    // as we write the tagline directly onto the bitmap.
	    tagLineSlop = 0;
	} else {
	    tagLineSlop = (tagLineFont->fontHeight()+MARGIN_TOP+MARGIN_BOT+SLOP_LINES) * 
		howmany(params.pageWidth(),8);
	}
	return (true);
    } else {
	tagLineSlop = 0;
	return (false);
    }
}

#include "MemoryDecoder.h"

/*
 * Image the tag line in place of the top few lines of the page
 * data and return the encoded tag line at the front of the
 * data buffer.  The buffer that holds the page data is assumed
 * to have tagLineSlop extra space allocated in front of the
 * page data.  The tag line format string is assumed to be
 * preprocessed by setupTagLine above so that we only need to
 * setup the current page number.
 */
u_char*
FaxModem::imageTagLine(u_char* buf, u_int fillorder, const Class2Params& params, u_long& totdata)
{
    u_int l;
#ifdef LC_CTYPE
    setlocale(LC_CTYPE, tagLineLocale);         // for <ctype.h> calls
#endif
#ifdef LC_TIME
    setlocale(LC_TIME, tagLineLocale);          // for strftime calls
#endif
    bool isutf8 = false;
#if HAS_LANGINFO
    isutf8 = (strcmp(nl_langinfo(CODESET), "UTF-8") == 0);
    tagLine.setUTF8(isutf8);
#endif
    /*
     * Fill in any per-page variables used in the tag line.
     */
    fxStr tag = tagLine;
    l = 0;
    while (l < tag.length()) {
	l = tag.next(l, '%');
	if (l >= tag.length()-1)
	    break;
	if (tag[l+1] == 'p')
	    insert(tag, l, fxStr((int) pageNumber, "%d"));
	if (tag[l+1] == 'P') {
	    if (noCountCoverPages)
		insert(tag, l, "-");
	    else
		insert(tag, l, fxStr((int) pageNumberOfJob, "%d"));
	} else
	    l += 2;
    }
    /* 
     * Setup the raster in which the tag line is imaged.
     *
     * The font size information received from the font functions
     * is suitable for VR_FINE.  Thus VR_FINE is used as the reference
     * resolution, and all other resolutions must be scaled.
     */
    u_int w = params.pageWidth();
    u_int h = (tagLineFont->fontHeight()*2)+MARGIN_TOP+MARGIN_BOT;	// max height - double VR_FINE
    u_int th = 0;							// actual tagline height
    switch(params.vr) {
	case VR_NORMAL:
	case VR_200X100:
	    th = (tagLineFont->fontHeight()/2)+MARGIN_TOP+MARGIN_BOT;	// half VR_FINE
	    break;
	case VR_FINE:
	case VR_200X200:
	    th = tagLineFont->fontHeight()+MARGIN_TOP+MARGIN_BOT;	// reference resolution
	    break;
	case VR_R8:
	case VR_R16:
	case VR_200X400:
	case VR_300X300:	// not proportionate but legible
	    th = (tagLineFont->fontHeight()*2)+MARGIN_TOP+MARGIN_BOT;	// double VR_FINE
	    break;
    }
    /*
     * imageText() works with u_short while encodeTagLine() accepts u_long.
     * However, encodeTagLine still expects u_short word boundaries.
     * raster is a buffer of u_short instead of u_long (even though using
     * u_long would save us some loop iterations) because at some resolutions
     * on 64-bit systems the longs-per-row are not congruent with the shorts-per-row.
     *
     * NB: The +SLOP_LINES below is for the case where we need to
     *     re-encode 2D-encoded data.  An extra 3 rows is sufficient
     *     because the number of consecutive 2D-encoded rows is bounded
     *     by the K parameter in the CCITT spec.
     */
    u_int spr = howmany(w,16);                         // shorts/raster row (per imageText)
    u_short* raster = new u_short[(h+SLOP_LINES)*spr]; // decoded raster
    memset(raster,0,(h+SLOP_LINES)*spr*sizeof (u_short));// clear raster to white
    /*
     * Break the tag into fields and render each piece of
     * text centered in its field.  Experiments indicate
     * that rendering the text over white is better than,
     * say, rendering it over the original page.
     */
    l = 0;
    /*
     * imageText produces good-dimensioned fonts at fine resolution.
     * For other resolutions we'll shrink or stretch the text.
     */
    u_int fieldWidth = params.pageWidth() / (params.vr == VR_R16 ? 2 : 1) / tagLineFields;
    for (u_int f = 0; f < tagLineFields; f++) {
	fxStr tagField = tag.token(l, '|');
	u_int align = 0;	// centered
	if (tagField.length()) {
	    if (tagField[0] == '{') {
		align = 1;		// left-aligned
		tagField.remove(0,1);
	    } else if (tagField[0] == '}') {
		align = 2;		// right-aligned
		tagField.remove(0,1);
	    }
	}
	u_int fw, fh;
	tagLineFont->strWidth(tagField, isutf8, fw, fh);
	u_int xoff = f*fieldWidth;
	if ((fw+MARGIN_LEFT+MARGIN_RIGHT) < fieldWidth) {
	    switch (align) {
		case 1 : xoff += MARGIN_LEFT; break;
		case 2 : xoff += fieldWidth-fw-MARGIN_RIGHT; break;
		default: xoff += (fieldWidth-fw)/2; break;
	    }
	} else
	    xoff += MARGIN_LEFT;
	(void) tagLineFont->imageText(tagField, isutf8, raster, w, h,
	    xoff, MARGIN_RIGHT, MARGIN_TOP, MARGIN_BOT);
    }

    /*
     * Scale image data as needed (see notes above).
     */

    if (params.vr == VR_NORMAL || params.vr == VR_200X100) {
	/*
	 * These resolutions require vertical "shrinking" of the
	 * tagline image.  We make 1 line out of 2.
	 * (Note the ``or'' used to generate the final samples.)
	 *
	 * Details:  
	 * - image is in lines 1 through y
	 * - available lines are 1 through y/2
	 * - start at the top of the image
	 * - line 1 is ORed with line 2 to get new line 1
	 * - line 3 is ORed with line 4 to get new line 2
	 * - ...
	 * - line y is ORed with line y+1 to get new line (y+1)/2
	 */
	u_short* s1 = raster + MARGIN_TOP*spr;
	u_short* s2 = s1+spr;
	u_short* s3 = raster + MARGIN_TOP*spr;
	for (u_int nr = tagLineFont->fontHeight(); nr; nr--) {
	    for (u_int nl = spr; nl; nl--)
		*s3++ = *s1++ | *s2++;
	    s1 += spr;
	    s2 += spr;
	}
	memset(s3, 0, MARGIN_BOT*spr*sizeof (u_short));
    }
    if (params.vr == VR_R8 || params.vr == VR_R16 || params.vr == VR_200X400 || params.vr == VR_300X300) {
	/*
	 * These resolutions require vertical "stretching" of the
	 * tagline image.  We make 2 lines out of 1.
	 * Go bottom-to-top since the image resides in the top half and the
	 * bottom data can be overwritten since it is unset.
	 * 
	 * Details:
	 * - image is in lines 1 through y/2
	 * - available lines are 1 through y
	 * - we use 2 pointers, 1st starting at line y/2 the other at line y
	 * - line y/2   copied in line y   and y-1
	 * - line y/2-1 copied in line y-2 and y-2-1
	 * - ...
	 */
	// beginning of bottom line of actual image
	u_short* s1 = raster + (spr * (tagLineFont->fontHeight()+MARGIN_TOP-1));
	// beginning of bottom line of target image
	u_short* s2 = s1 + (spr * (tagLineFont->fontHeight()));

	for (u_int nr = tagLineFont->fontHeight(); nr; nr--) {
	    memcpy(s2, s1, spr*sizeof(u_short));// copy
	    s2 -= spr;				// move s2 up one line
	    memcpy(s2, s1, spr*sizeof(u_short));// copy again
	    s2 -= spr;				// move s2 up one line
	    s1 -= spr;				// move s1 up one line
	}
	if (params.vr == VR_R16) {
	    /*
	     * hr is twice the hr in which data is imaged.
	     * We need to strech the image horizontally:
	     * 1234567890ABCDEFGHIJ -> 11223344556677889900
	     * (ABCDEFGHIJ is whitespace)
	     */

	    /* Reset ptr to begin of image */
	    s1 = raster + MARGIN_TOP*spr;	// begin of 1st line
	    s2 = s1 + spr - 1;			// end of 1st line
	    for (u_int nr = tagLineFont->fontHeight()*2; nr; nr--) {
		/*
		 * 0      spr/2      spr
		 * |        |         |
		 * 1234567890__________
		 * 1234567890________00  x/2   copied into x   and x-1
		 * 1234567890______9900  x/2-1 copied into x-2 and x-3
		 * ...
		 * 11223344556677889900
		 */
		u_int bps = sizeof(u_short) * 8;	// bits per u_short
		for (u_int nl = spr/2 - 1; nl ; nl--) {
		    // make 2 shorts out of 1 (ABCD -> AABB CCDD)
		    int pos = 0;
		    for (u_int i = 0; i < (bps/8); i++) {
			if (i == 0 || i == bps/8/2) {
			    *s2 = (u_short) 0;
			    pos = bps - 2;
			}
			// put pairs of bits from s1 into the right places within s2
			*s2 |= (u_short)((*(s1+nl) & (1<<(bps-8*i-5))) >> (bps-8*i-5) << pos);
			*s2 |= ((u_short)((*(s1+nl) & (1<<(bps-8*i-5))) >> (bps-8*i-5) << pos)) << 1;
			pos -= 2;

			*s2 |= (u_short)((*(s1+nl) & (1<<(bps-8*i-6))) >> (bps-8*i-6) << pos);
			*s2 |= ((u_short)((*(s1+nl) & (1<<(bps-8*i-6))) >> (bps-8*i-6) << pos)) << 1;
			pos -= 2;

			*s2 |= (u_short)((*(s1+nl) & (1<<(bps-8*i-7))) >> (bps-8*i-7) << pos);
			*s2 |= ((u_short)((*(s1+nl) & (1<<(bps-8*i-7))) >> (bps-8*i-7) << pos)) << 1;
			pos -= 2;

			*s2 |= (u_short)((*(s1+nl) & (1<<(bps-8*i-8))) >> (bps-8*i-8) << pos);
			*s2 |= ((u_short)((*(s1+nl) & (1<<(bps-8*i-8))) >> (bps-8*i-8) << pos)) << 1;
			pos -= 2;

			*s2 |= (u_short)((*(s1+nl) & (1<<(bps-8*i-1))) >> (bps-8*i-1) << pos);
			*s2 |= ((u_short)((*(s1+nl) & (1<<(bps-8*i-1))) >> (bps-8*i-1) << pos)) << 1;
			pos -= 2;

			*s2 |= (u_short)((*(s1+nl) & (1<<(bps-8*i-2))) >> (bps-8*i-2) << pos);
			*s2 |= ((u_short)((*(s1+nl) & (1<<(bps-8*i-2))) >> (bps-8*i-2) << pos)) << 1;
			pos -= 2;

			*s2 |= (u_short)((*(s1+nl) & (1<<(bps-8*i-3))) >> (bps-8*i-3) << pos);
			*s2 |= ((u_short)((*(s1+nl) & (1<<(bps-8*i-3))) >> (bps-8*i-3) << pos)) << 1;
			pos -= 2;

			*s2 |= (u_short)((*(s1+nl) & (1<<(bps-8*i-4))) >> (bps-8*i-4) << pos);
			*s2 |= ((u_short)((*(s1+nl) & (1<<(bps-8*i-4))) >> (bps-8*i-4) << pos)) << 1;
			pos -= 2;
			if (pos < 0) *s2--;
		    }
		}
		s1 += spr;		// begin of next line
		s2 = s1 + spr - 1;	// end of next line
	    }
	}
    }
    if (params.jp) {
	/*
	 * The image buffer is already a decoded bitmap.  
	 * So we need to put the raster onto the bitmap now.
	 *
	 * First white-out the image behind the text.
	 * Then make a pass and put the text on in black.
	 */
	memset(buf, 0xFF, th*w*3);

	u_char* braster = (u_char*) raster;	// raster is byte-aligned not long-aligned
	u_int p;
	for (l = 0; l < (th*w)/8; l++) {
	    if (braster[l]) {
		for (p = 0; p < 8; p++) {
		    if (braster[l] & (1 << (7-p))) {
			// (l * 8 + p) gives the pixel number
			memset((buf+(l*8+p)*3), 0x00, 3);	// set it black
		    }
		}
	    }
	}
	return (buf);
    }
    MemoryDecoder dec(buf, w, totdata, fillorder, params.is2D(), (params.df == DF_2DMMR));
    u_char* encbuf = dec.encodeTagLine((u_long*) raster, th, tagLineSlop);   
    totdata = dec.getCC();
    return (encbuf);
}
