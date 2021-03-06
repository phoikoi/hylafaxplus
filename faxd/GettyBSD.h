/*	$Id: GettyBSD.h 2 2005-11-11 21:32:03Z faxguy $ */
/*
 * Copyright (c) 1993-1996 Sam Leffler
 * Copyright (c) 1993-1996 Silicon Graphics, Inc.
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
#ifndef _BSDGETTY_
#define	_BSDGETTY_

/*
 * BSD-style getty support that
 * just invokes the getty program.
 */
#include "Getty.h"

#ifdef HAS_UTMPX
#define UTMPSTRUCT utmpx
#define _PATH_UTMP _PATH_VARRUN"/utx.active"
#define _PATH_WTMP "/var/log/utx.log"
#define UTIME ut_tv.tv_sec
#define UNAME ut_user
#else
#define UTMPSTRUCT utmp
#define UTIME ut_time
#define UNAME ut_name
#endif

struct UTMPSTRUCT;

class BSDSubProc : public Getty {
protected:
    void setupSession(int modemFd);
public:
    BSDSubProc(const char* path, const fxStr& dev, const fxStr& speed);
    ~BSDSubProc();
};

class BSDGetty : public BSDSubProc {
protected:
    void setupSession(int modemFd);
private:
    void writeWtmp(UTMPSTRUCT* ut);
    void logout(const char* line);
public:
    BSDGetty(const char* path, const fxStr& dev, const fxStr& speed);
    ~BSDGetty();

    void hangup();
};
#endif /* _BSDGETTY_ */
